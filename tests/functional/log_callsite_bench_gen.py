#!/usr/bin/python
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

# expected to work with both Python 2.6+/3+
from __future__ import print_function

"""Generate callsite-heavy logging client so as to evaluate use of resources"""

from getopt import GetoptError, getopt
from math import ceil, floor, log10
#from pprint import pprint
#from random import shuffle
from sys import argv, exit

def die(*args, **kwargs):
    print(*args, **kwargs)
    exit(1)

def list_to_c_source(worklist, fnc_prefix, width=0):
    ret = []

    while worklist:
        item = worklist.pop()
        if type(item) is list:
            head, children = item
            if type(children) is list:
                for i, ci in enumerate(children):
                    ret += list_to_c_source([ci], fnc_prefix, width)
                    if type(ci) is list:
                        children[i] = ci[0]
                        if type(children[i]) is list:
                            children[i] = children[i][0]
        else:
            head = item
            children = []
        if type(head) is not list:
            head = [head]
        ret += ["static void {0}_{1:0{2}}(int doit) {{"
                .format(fnc_prefix, head[0], width),
                "\tif (!doit) return;"]
        ret += ["\tqb_log(LOG_ERR, \"{0:0{1}}\");".format(i, width)
                for i in head]
        ret += ["\t{0}_{1:0{2}}(doit);".format(fnc_prefix, i, width)
                for i in reversed(children)]
        ret += ["}"]
    return ret

def main(opts, args):
    FNC_PREFIX = "fnc"

    try:
        CALLSITE_COUNT = int(opts["CALLSITE_COUNT"])
        if not 0 < CALLSITE_COUNT < 10 ** 6: raise ValueError
    except ValueError:
        die("callsites count can only be a number x, 0 < x < 1e6")
    try:
        BRANCHING_FACTOR = int(opts["BRANCHING_FACTOR"])
        if not 0 < BRANCHING_FACTOR < 10 ** 3: raise ValueError
    except ValueError:
        die("branching factor can only be a number x, 0 < x < 1000")
    try:
        CALLSITES_PER_FNC = int(opts["CALLSITES_PER_FNC"])
        if not 0 < CALLSITES_PER_FNC < 10 ** 3: raise ValueError
    except ValueError:
        die("callsites-per-fnc count can only be a number x, 0 < x < 1000")
    try:
        ROUND_COUNT = int(opts["ROUND_COUNT"])
        if not 0 < ROUND_COUNT < 10 ** 6: raise ValueError
    except ValueError:
        die("round count can only be a number x, 0 < x < 1e6")

    worklist, worklist_len = list(range(0, CALLSITE_COUNT)), CALLSITE_COUNT
    #shuffle(worklist)

    #pprint(worklist)
    first = worklist[0]
    while worklist_len > 1:
        item = worklist.pop(); worklist_len -= 1
        reminder = worklist_len % CALLSITES_PER_FNC
        parent = (worklist_len - reminder if reminder
                  else (worklist_len // CALLSITES_PER_FNC - 1)
                        // BRANCHING_FACTOR * CALLSITES_PER_FNC)
        #print("parent {0} (len={1})".format(parent, worklist_len))
        if type(worklist[parent]) is not list:
            worklist[parent] = [worklist[parent], []]
        if not(reminder):
            worklist[parent][1].append(item)  # reverses the order!
            #worklist[parent][1][:0] = [item]
        else:
            if type(worklist[parent][0]) is not list:
                worklist[parent][0] = [worklist[parent][0]]
            #worklist[parent][0].append(item)  # reverses the order
            worklist[parent][0][1:1] = [item]  # parent itself the 1st element
        #pprint(worklist)

    width = int(floor(log10(CALLSITE_COUNT))) + 1
    print('\n'.join([
        "/* compile with -lqb OR with -DQB_KILL_ATTRIBUTE_SECTION -lqb */",
        "#include <qb/qblog.h>",
    ] + list_to_c_source(worklist, FNC_PREFIX, width) + [
        "int main(int argc, char *argv[]) {",
        "\tqb_log_init(\"log_gen_test\", LOG_DAEMON, LOG_INFO);",
        "\tqb_log_ctl(QB_LOG_SYSLOG, QB_LOG_CONF_ENABLED, QB_FALSE);",
        "\tqb_log_filter_ctl(QB_LOG_STDERR, QB_LOG_FILTER_ADD, QB_LOG_FILTER_FILE, \"*\", LOG_ERR);",
        "\tqb_log_ctl(QB_LOG_STDERR, QB_LOG_CONF_ENABLED, QB_TRUE);",
        "\tfor (int i = 0; i < {0}; i++) {{".format(ROUND_COUNT),
        "\t\t{0}_{1:0{2}}(argc);".format(FNC_PREFIX, first, width),
        "\t}",
        "\tqb_log_fini();",
        "\treturn !argc;",
        "}"
    ]
    ))


if __name__ == '__main__':
    # Full trees for CALLSITES_PER_FNC == 1 (can be trivially extrapolated):
    # BF = 2 (binary trees)
    # --> C = 7 (3 steps), 15 (4 steps), ..., 127 (6 steps), ...
    #     (see https://en.wikipedia.org/wiki/Binary_tree#Properties_of_binary_trees)
    # BF = 3 (ternary trees)
    # --> C = 13 (3 steps), 40 (4 steps), ..., 1093 (6 steps), ...
    #     (see https://en.wikipedia.org/wiki/Ternary_tree#Properties_of_ternary_trees)
    # ...
    BRANCHING_FACTOR = 3
    CALLSITES_PER_FNC = 10
    CALLSITE_COUNT = 3640
    ROUND_COUNT = 1000
    try:
        opts, args = getopt(argv[1:],
                             "hc:b:f:r:",
                            ("help", "callsite-count=", "branching-factor=",
                             "callsites-per-fnc=", "round-count="))
        for o, a in opts:
            if o in ("-h", "--help"):
                raise GetoptError("__justhelp__")
            elif o in ("-c", "--callsite-count"): CALLSITE_COUNT = a
            elif o in ("-b", "--branching-factor"): BRANCHING_FACTOR = a
            elif o in ("-f", "--callsites-per-fnc"): CALLSITES_PER_FNC = a
            elif o in ("-r", "--round-count"): ROUND_COUNT = a
    except GetoptError as err:
        if err.msg != "__justhelp__":
            print(str(err))
        print("Usage:\n{0} -h|--help\n"
              "{0} [-c X|--callsite-count={CALLSITE_COUNT}]"
              " [-b Y|--branching-factor={BRANCHING_FACTOR}]\n"
              "{1:{2}} [-f Z|--callsites-per-fnc={CALLSITES_PER_FNC}]"
              " [-r R|--round-count={ROUND_COUNT}]"
              .format(argv[0], '', len(argv[0]), **locals()))
        exit(0 if err.msg == "__justhelp__" else 2)

    opts = dict(CALLSITE_COUNT=CALLSITE_COUNT,
                BRANCHING_FACTOR=BRANCHING_FACTOR,
                CALLSITES_PER_FNC=CALLSITES_PER_FNC,
                ROUND_COUNT=ROUND_COUNT)
    main(opts, args)
