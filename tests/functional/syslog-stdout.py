#!/usr/bin/python3
# Copyright 2017 Red Hat, Inc.
#
# Author: Jan Pokorny <jpokorny@redhat.com>
#
# This file is part of libqb.
#
# libqb is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2.1 of the License, or
# (at your option) any later version.
#
# libqb is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with libqb.  If not, see <http://www.gnu.org/licenses/>.

"""Simple /dev/log to stdout forwarding"""

import socket
from atexit import register
from os import remove
from sys import argv

# no locking, but anyway
try:
    remove("/dev/log")
except FileNotFoundError:
    pass
sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
sock.bind("/dev/log")

def shutdown():
    sock.close()
    remove("/dev/log")

def main(*argv):
    register(shutdown)
    while True:
        try:
            b = sock.recv(4096)
            # flushing is crucial here
            print(">>> " + str(b, 'ascii').split(' ', 3)[-1], flush=True)
        except IOError:
            pass

if __name__ == '__main__':
    main(*argv)
