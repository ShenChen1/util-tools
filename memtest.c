#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <asm/memory.h>

static char *mem_test_area;

static unsigned long pages_to_kb(unsigned long num)
{
	return num << (PAGE_SHIFT - 10);
}

static int test_mem(char *mem_test_area, unsigned long *mem_test_area_size)
{
	unsigned long i = 0, j = 0;
	int test_state = 0;
	int test_mem_pattern = 0x5;
	u64 broken_page = 0;
	u64 broken_page_checked = 0;
	char *bp = NULL;

	memset(mem_test_area, test_mem_pattern, *mem_test_area_size);
	while (i < *mem_test_area_size) {
		if (*(mem_test_area + i) != test_mem_pattern) {
			broken_page = PFN_PHYS(vmalloc_to_pfn((mem_test_area + i)));
			bp = phys_to_virt(broken_page);
			if (broken_page_checked != broken_page) {
				for (j = 0; j < PAGE_SIZE; j++) {
					if (*(bp + j) != test_mem_pattern) {
						pr_emerg("memtest: ERROR: memory corruption occurred at pyhsical address: %pa\n",
							 virt_to_phys((bp + j)));
						pr_emerg("memtest: ERROR: memory content was initially 0x%x now 0x%x\n",
							 test_mem_pattern, *(bp + j));
					}
				}
				broken_page_checked = broken_page;
			}
			test_state = -EILSEQ;
		}
		i++;
	}
	if (test_state == 0)
		pr_emerg("memtest: PASSED\n");

	return test_state;
}

static int memtest_init(void)
{
	struct sysinfo si;
	int max_runs = 3;
	int i = 0;
	int ret = 0;
	int test_state = 0;
	unsigned long available;
	unsigned long mem_test_area_size;

	si_meminfo(&si);
	available = pages_to_kb(si_mem_available());
	pr_emerg("memtest: before test: MemTotal: %lu kB, MemFree: %lu kB, MemAvailable: %lu kB\n",
		 pages_to_kb(si.totalram), pages_to_kb(si.freeram), available);

	/* available memory - 50 MB */
	mem_test_area_size = available * 1024 - 52428800;
	pr_emerg("memtest: allocating %lu kB for test\n",
		 mem_test_area_size / 1024);
	mem_test_area = vmalloc(mem_test_area_size);
	if (!mem_test_area) {
		pr_emerg("memtest: failed to vmalloc %lu\n",
			 mem_test_area_size / 1024);
		return -ENOMEM;
	}
	si_meminfo(&si);
	available = pages_to_kb(si_mem_available());

	pr_emerg("memtest: during test: MemTotal: %lu kB, MemFree: %lu kB, MemAvailable: %lu kB\n",
		 pages_to_kb(si.totalram), pages_to_kb(si.freeram), available);
	pr_emerg("memtest: test memory VIRT start addr = 0x%p, end addr = 0x%p\n",
		 mem_test_area, (mem_test_area + mem_test_area_size));

	for (i = 1; i < max_runs + 1; i++) {
		pr_emerg("memtest: starting test %d of %d\n", i, max_runs);
		ret = test_mem(mem_test_area, &mem_test_area_size);
		if (ret < 0) {
			pr_emerg("memtest: FAILED - test ends!\n");
			goto out;
		}
	}

	pr_emerg("memtest: SUCCESS - test ends!\n");

out:
	vfree(mem_test_area);
	mem_test_area = NULL;

	return test_state;
}
module_init(memtest_init);

static void __exit memtest_exit(void)
{
	if (mem_test_area)
		vfree(mem_test_area);
}
module_exit(memtest_exit);
