#!/bin/sh
#
# create a normal blackbox
rm -f crash-test-dummy.fdata
./crash_test_dummy

. ./test.conf

# first test that reading the valid
# blackbox data actually works.
../tools/qb-blackbox crash-test-dummy.fdata
if [ $? -ne 0 ]; then
	exit 1
fi


for i in $(seq $NUM_BB_TESTS)
do
    rm -f butchered_blackbox.fdata
    echo " ==== Corrupt blackbox test $i/$NUM_BB_TESTS ===="
    ./file_change_bytes -i crash-test-dummy.fdata -o butchered_blackbox.fdata -n 1024
    ../tools/qb-blackbox butchered_blackbox.fdata
    [ $? -gt 127 ] && exit 1 || true
done

exit 0
