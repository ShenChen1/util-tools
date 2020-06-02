#include <linux/cpumask.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/semaphore.h>

#include <linux/device.h>
#include <linux/of.h>
#include <linux/errno.h>
#include <asm/page.h>
#include <asm/current.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>

/* kthread sample */
#include <linux/kthread.h>
#include <linux/cpumask.h>

#define NUM_THREADS 10
#define NUM_ITERATIONS 100000

volatile static long locktest_counter;
static long locktest_counter2;

static DEFINE_SPINLOCK(locktest_spinlock);
static DEFINE_SEMAPHORE(locktest_semaphore);
static int locktest_thread_done[NUM_THREADS];

static int threads = -1;
static int iters = NUM_ITERATIONS;

#define KTHREAD_NAME_MAX 32

static int locktest_thread_spinlock(void *data)
{
    int i;

    for (i = 0; i < NUM_ITERATIONS; i++) {
        spin_lock(&locktest_spinlock);
        locktest_counter++;
        spin_unlock(&locktest_spinlock);
    }

    *(int *)data = 1;
    smp_mb();

    do_exit(0);
}

static int locktest_thread_semaphore(void *data)
{
    int i;

    for (i = 0; i < NUM_ITERATIONS; i++) {
        int ret = down_interruptible(&locktest_semaphore);
        if (ret)
            do_exit(ret);
        locktest_counter++;
        up(&locktest_semaphore);
    }

    *(int *)data = 1;
    smp_mb();

    do_exit(0);
}

static int locktest_perform_test(char *buf, size_t size, int (*threadfn)(void *data), char *testname)
{
    int i;
    int threads = 0;
    int ret;
    int total = 0;

    locktest_counter = 0;

    for (i = 0; i < NUM_THREADS; i++) {
        struct task_struct *threadptr;

        locktest_thread_done[i] = 0;

        threadptr = kthread_run(threadfn, &locktest_thread_done[i], "locktest_thread%d", i);
        if (IS_ERR(threadptr)) {
            ret = snprintf(buf, size - total, "kthread_run() failed\n");
            total += ret;
            buf += ret;
            break;
        }

        threads++;
    }

    while (threads--)
        while (!locktest_thread_done[threads])
            msleep(1);

    if (locktest_counter != NUM_THREADS * NUM_ITERATIONS)
        ret = snprintf(buf, size - total, "%s test failed: %ld instead of %d\n", testname, locktest_counter, NUM_THREADS * NUM_ITERATIONS);
    else
        ret = snprintf(buf, size - total, "%s test passed\n", testname);
    total += ret;

    return total;
}

static int locktest_thread_spinlock2(void *data)
{
    int i, iters_local = iters;

    for (i = 0; i < iters_local; i++) {
        spin_lock(&locktest_spinlock);
        locktest_counter2++;
        spin_unlock(&locktest_spinlock);
    }

    *(int *) data = true;

    do_exit(0);
}

static int locktest_thread_semaphore2(void *data)
{
    int i, iters_local = iters;

    for (i = 0; i < iters_local; i++) {
        int ret = down_interruptible(&locktest_semaphore);
        if (ret)
            return 0;
        locktest_counter2++;
        up(&locktest_semaphore);
    }

    *(int *) data = true;

    do_exit(0);
}

/*
 * Main test-executing function
 *
 * Function will start @threads number of kthreads, binded
 * to cpus where threads will be round robined to available
 * cpus. test_function will be given as entry point to these
 * kthreads.
 *
 * It is task of test_function to implement int flag being set
 * to true when function is done.
 *
 * */
static int start_test(int (*test_function)(void *))
{
    char kthread_name[KTHREAD_NAME_MAX];
    int cpu = -1, err = 0, threads_local, i;
    int *kthread_done;
    struct task_struct **kthreads;

    locktest_counter2 = 0;
    threads_local = threads;

    kthreads = (struct task_struct **) kmalloc(threads_local * sizeof(struct task_struct*), GFP_KERNEL);
    if (kthreads == NULL) {
        pr_err("%s: Failed to allocate array of task pointers\n", __func__);
        return -ENOMEM;
    }

    kthread_done = (int *) kzalloc(threads_local * sizeof(int), GFP_KERNEL);
    if (kthread_done == NULL) {
        pr_err("%s: Failed to allocate array of flags\n", __func__);
        return -ENOMEM;
    }

    for(i = 0; i < threads_local; i++) {
        cpu = cpumask_next(cpu, cpu_online_mask);
        snprintf(kthread_name, KTHREAD_NAME_MAX, "test_kthread.%d", cpu);
        kthreads[i] = kthread_create_on_node(test_function,
                                             &kthread_done[i],
                                             cpu_to_node(cpu),
                                             kthread_name);
        if (IS_ERR(kthreads[i])) {
            pr_err("%s: Failed to create kthread on cpu %d!\n", __func__, cpu);
            err = -1;
            break;
        }
        kthread_bind(kthreads[i], cpu);
        wake_up_process(kthreads[i]);
    }

    while (i--)
        while (!kthread_done[i])
            msleep(1);

    kfree(kthreads);
    kfree(kthread_done);

    return err;
}

/*
 * Interface to userspace
 */

/*
 * Executes spinlock test taking into account threads and iterations variable
 * To execute, echo anything into it
 */
static ssize_t run_spinlock_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int err;

    pr_info("%s: Starting spinlock test, threads %d, iterations %d\n", __func__, threads, iters);

    err = start_test(locktest_thread_spinlock2);
    if (err) {
        pr_err("%s: Test returned error!\n", __func__);
        return err;
    }

    pr_info("%s: done, locktest_counter = %ld", __func__, locktest_counter2);

    return count;
}

static ssize_t run_spinlock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "echo anything to run\n");
}

/*
 * Executes semaphore test taking into account threads and iterations variable
 * To execute, echo anything into it
 */
static ssize_t run_semaphore_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int err;

    pr_info("%s: Starting semaphore test, threads %d, iterations %d\n", __func__, threads, iters);

    err = start_test(locktest_thread_semaphore2);
    if (err) {
        pr_err("%s: Test returned error!\n", __func__);
        return err;
    }

    pr_info("%s: done, locktest_counter = %ld", __func__, locktest_counter2);

    return count;
}

static ssize_t run_semaphore_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "echo anything to run\n");
}

/*
 * Number of iterations to run each thread. Taken into account on next run
 */
static ssize_t iters_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int err;

    err = sscanf(buf, "%d", &iters);
    if (err != 1) {
        pr_err("%s: Failed to parse <%s> into integer\n", __func__, buf);
        return -EINVAL;
    } else {
        pr_info("%s: Number of iterations set to %d\n", __func__, iters);
    }

    return count;
}

static ssize_t iters_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "%d\n", iters);
}

/*
 * Number of kthreads to start. Taken into account on next run
 * */
static ssize_t threads_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int err;

    err = sscanf(buf, "%d", &threads);
    if (err != 1) {
        pr_err("%s: Failed to parse <%s> into integer\n", __func__, buf);
        return -EINVAL;
    } else {
        pr_info("%s: Number of threads set to %d\n", __func__, iters);
    }

    return count;
}

static ssize_t threads_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "%d\n", threads);
}

static ssize_t run_basic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    size_t limit = PAGE_SIZE;
    int ret;

    ret = snprintf(buf, limit, "Using %u bytes long variable for test\n", (unsigned)sizeof(locktest_counter));
    buf += ret;
    limit -= ret;

    ret = locktest_perform_test(buf, limit, locktest_thread_spinlock, "spinlock");
    buf += ret;
    limit -= ret;

    ret = locktest_perform_test(buf, limit, locktest_thread_semaphore, "semaphore");
    limit -= ret;

    return PAGE_SIZE - limit;
}

/*
 * Counter result. Use it after test is done, on the beginning of each test is anyway resetted
 * */
static ssize_t locktest_counter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PAGE_SIZE, "%ld\n", locktest_counter2);
}

static struct device locktest_device;

static DEVICE_ATTR(run_spinlock, S_IRUGO | S_IWUSR | S_IWGRP, run_spinlock_show, run_spinlock_store);
static DEVICE_ATTR(run_semaphore, S_IRUGO | S_IWUSR | S_IWGRP, run_semaphore_show, run_semaphore_store);
static DEVICE_ATTR(iters, S_IRUGO | S_IWUSR | S_IWGRP, iters_show, iters_store);
static DEVICE_ATTR(threads, S_IRUGO | S_IWUSR | S_IWGRP, threads_show, threads_store);
static DEVICE_ATTR(run_basic, S_IRUGO, run_basic_show, NULL);
static DEVICE_ATTR(locktest_counter, S_IRUGO, locktest_counter_show, NULL);

static struct attribute *locktest_attr[] = {
    &dev_attr_run_spinlock.attr,
    &dev_attr_run_semaphore.attr,
    &dev_attr_run_basic.attr,
    &dev_attr_iters.attr,
    &dev_attr_threads.attr,
    &dev_attr_locktest_counter.attr,
    NULL
};

static struct attribute_group locktest_attr_grp = {
    .attrs = locktest_attr
};

static void locktest_dev_release(struct device *device)
{
    pr_info("%s:\n", __func__);
}

static struct device_type locktest_dev_type = {
    .name = "locktest_device",
    .release = locktest_dev_release,
};

static int __init locktest_init(void)
{
    int err;

    threads = num_online_cpus();

    pr_info("%s: Initializing locktest with %d cpus\n", __func__, threads);

    locktest_device.type = &locktest_dev_type;
    dev_set_name(&locktest_device, "locktest");

    err = device_register(&locktest_device);
    if(err) {
        pr_err("%s: Failed to register device\n", __func__);
        goto exit;
    }

    err = sysfs_create_group(&locktest_device.kobj, &locktest_attr_grp);
    if(err) {
        pr_err("%s: Failed to add sysfs entries\n", __func__);
        goto sysfs_failed;
    }

    return 0;

sysfs_failed:
    device_unregister(&locktest_device);

exit:
    return -1;
}

static void __exit locktest_exit(void)
{
    device_unregister(&locktest_device);
}

module_init(locktest_init);
module_exit(locktest_exit);

MODULE_DESCRIPTION("Lock functions test");
MODULE_LICENSE("GPL");
