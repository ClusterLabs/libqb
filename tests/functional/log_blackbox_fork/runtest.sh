#!/bin/sh
trap "echo finished; rm -f /dev/shm/qb-*-blackbox-[dh]*; exit" TERM
wrapper="prlimit --nproc=$(sysctl -n kernel.pid_max): --core=unlimited:"
#wrapper="gdb -args"
libtool --mode=execute ${wrapper} ./log_blackbox_fork
