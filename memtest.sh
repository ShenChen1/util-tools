#!/bin/bash

# prevent some megabytes from being used for memtest tmpfs
SPARE=50

# prevent some megabytes in tmpfs from being used for memtest
TMPFSSPARE=5

# size of testfile in MB
TESTFILE_SIZE=10

# amount of default threads
THREADS=6

TESTDIR="/mnt/memtest"

if [[ "$#" == "1" ]]
then
	if (( $1 < 1 || $1 > 14))
	then
		echo "usage: $0 <threads>"
		echo "           threads can be 1-14"
		exit
	fi
	THREADS=$1
fi

_PPID=$$


kill_threads()
{
	echo -e '\n\nctrl+c hit, killing all threads...'
	ps -o pid= --ppid=$_PPID |
	while read line
	do
		kill $line &> /dev/null
	done
	echo "stopping dd..."
	ps -o pid= -o comm= | grep dd |
	while read line
	do
		kill $line &> /dev/null
	done
	wait
	sleep 1
	echo "umounting tmpfs..."
	umount "$TESTDIR"
	echo "done"
	exit 0
}

trap kill_threads INT
trap 'exit 0' TERM

err()
{
	echo "ERROR! --> $@" 1>&2
}

if mount | grep "$TESTDIR" &> /dev/null
then
	echo "cleaning up previous run..."
	umount "$TESTDIR"
fi

if [[ ! -d "$TESTDIR" ]]
then
	mkdir -p "$TESTDIR"
fi

MEM=($(free -m | sed -n "2p"))

TOTAL=${MEM[1]}
USED=${MEM[2]}
FREE=${MEM[3]}

TMPFS_SIZE=$(( FREE - SPARE ))
TEST_MEM=$(( TMPFS_SIZE - TMPFSSPARE ))

CNT_TESTFILES=$(( TEST_MEM / (THREADS * TESTFILE_SIZE) ))

echo "Memory: total=${TOTAL}MB free=${FREE}MB used=${USED}MB"
echo "  memtest will mount ${TMPFS_SIZE}MB and test about ${TEST_MEM}MB (${CNT_TESTFILES} * ${THREADS} * ${TESTFILE_SIZE}MB)"

mount -o size=${TMPFS_SIZE}M -t tmpfs none "$TESTDIR"

if (( $? ))
then
	err "cannot mount $TESTDIR"
	exit 1
fi

echo
echo "Testing if all ${TEST_MEM}MB can be written..."

for ((i = 0; i < THREADS; ++i))
do
	(
		sleep 1
		for ((j = 0; j < CNT_TESTFILES; ++j))
		do
			if ! dd if=/dev/zero of="${TESTDIR}/zero_${i}_${j}" bs=1M count=${TESTFILE_SIZE} &> /dev/null
			then
				exit 1
			fi
		done
	) &
	PID[$i]=$!
	echo "    started pretest thread: ${PID[$i]}"
done

for ((i = 0; i < THREADS; ++i))
do
	wait ${PID[$i]}
	if (( $? ))
	then
		err "test writing failed, please increase spare sizes"
		exit 1
	fi
done

rm -f ${TESTDIR}/zero_*

echo
echo "pre test successful, now starting real test..."

RUN=0
while true
do
	echo
	echo "-----------------------"
	echo "run: $((++RUN))"
	date
	echo
	echo "df of testramdisk:"
	echo "-----------------------"
	df -h "$TESTDIR"
	for ((i = 0; i < THREADS; ++i))
	do
		(
			sleep 1
			if ! dd if=/dev/urandom of="${TESTDIR}/data_${i}_0" bs=1M count=${TESTFILE_SIZE} &> /dev/null
			then
				exit 1
			fi
			for ((j = 1; j < CNT_TESTFILES; ++j))
			do
				cp "${TESTDIR}/data_${i}_0" "${TESTDIR}/data_${i}_${j}"
			done

			MD5=$(md5sum < "${TESTDIR}/data_${i}_0")
			echo "      md5sum Thread $i: $MD5"

			for ((j = 1; j < CNT_TESTFILES; ++j))
			do
				if [[ "$MD5" != $(md5sum < "${TESTDIR}/data_${i}_${j}") ]]
				then
					err "md5sum differs THREAD=$i FILE=${TESTDIR}/data_${i}_${j}"
					exit 1
				fi
			done
		) &
		PID[$i]=$!
		echo "  started thread: ${PID[$i]}"
	done

	for ((i = 0; i < THREADS; ++i))
	do
		wait ${PID[$i]}
		if (( $? ))
		then
			err "MEMORY TEST FAILED!"
			exit 1
		fi
	done
done

exit 0