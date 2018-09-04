#!/bin/sh
# Run this to generate all the initial makefiles, etc.

version_files=".snapshot-version .tarball-version .version"
work_tree=0; test -n "$(cat $version_files 2>/dev/null|head -n1)" || work_tree=1
autoreconf_opts=${autoreconf_opts=-i -v}; test $# -eq 0 || autoreconf_opts=$*
# -f to always adapt to actual project's version dynamically @ dev's checkout
test $work_tree -eq 0 || autoreconf_opts="$autoreconf_opts -f"

autoreconf $autoreconf_opts || exit $?
if grep -Eq '\-yank' $version_files 2>/dev/null; then
    echo ': CONSUME SNAPSHOTS ONLY AT YOUR RISK (genuine releases recommended!)'
    printf ': snapshot version: '
elif test $work_tree -eq 0; then
    echo ': About to consume a source distribution (genuine release advised)...'
    printf ': tracked version: '
else
    echo ': About to consume a checked out tree (dedicated for maintenance!)...'
fi
cat $version_files 2>/dev/null | head -n1; rm -f .snapshot-version

echo ': Now run ./configure && make'
