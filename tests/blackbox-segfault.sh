#!/bin/sh
#
# create a normal blackbox

# use in-tree qb-blackbox when available
if [ -f ../tools/qb-blackbox ]; then
 QBBLACKBOX=../tools/qb-blackbox
else
 QBBLACKBOX=qb-blackbox
fi

#
# create a normal blackbox
#
rm -f crash-test-dummy.fdata
./crash_test_dummy
rm -f core*

. ./test.conf

# first test that reading the valid
# blackbox data actually works.
$QBBLACKBOX crash-test-dummy.fdata
if [ $? -ne 0 ]; then
	exit 1
fi

for i in $(seq $NUM_BB_TESTS)
do
    rm -f butchered_blackbox.fdata
    echo " ==== Corrupt blackbox test $i/$NUM_BB_TESTS ===="
    ./file_change_bytes -i crash-test-dummy.fdata -o butchered_blackbox.fdata -n 1024
    $QBBLACKBOX butchered_blackbox.fdata
    [ $? -gt 127 ] && exit 1 || true
done

exit 0
