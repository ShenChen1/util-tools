#!/bin/bash

locktest() {

    insmod locktest.ko
    if (($? != 0)); then
        echo "Failed to load locktest module"
        return 1
    fi

    # config paramter
    printf 1000000 > /sys/devices/locktest/iters

    iters=$(cat /sys/devices/locktest/iters)
    threads=$(cat /sys/devices/locktest/threads)
    expected=$((iters * threads))

    echo "Running ... iterations $iters, threads $threads"

    spinlock_test_time=$(TIMEFORMAT=%R; time echo test > /sys/devices/locktest/run_spinlock)
    if (($? != 0)); then
        echo "Failed to run locktest-spinlock test"
        return 1
    fi

    counter_result=$(cat /sys/devices/locktest/locktest_counter)
    if (($? != 0)); then
        echo "Failed to get counter result"
        return 1
    fi
    echo "spinlock test took $spinlock_test_time, result (iterations x threads) $counter_result"
    if (($expected != $counter_result)); then
        echo "Result doesnt match to expected - locking broken?"
        return 1
    fi

    semaphore_test_time=$(TIMEFORMAT=%R; time echo test > /sys/devices/locktest/run_semaphore)
    if (($? != 0)); then
        echo "Failed to run locktest-semaphore test"
        return 1
    fi

    counter_result=$(cat /sys/devices/locktest/locktest_counter)
    if (($? != 0)); then
        echo "Failed to get counter result"
        return 1
    fi
    echo "semaphore test took $semaphore_test_time, result (iterations x threads) $counter_result"
    if (($expected != $counter_result)); then
        echo "Result doesnt match to expected - locking broken?"
        return 1
    fi

    echo "locking_spinlock_time;$spinlock_test_time"
    echo "locking_sempahore_time;$semaphore_test_time"
}

# main
locktest
