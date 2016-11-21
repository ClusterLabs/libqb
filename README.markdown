# libqb

## What is libqb?
libqb is a library with the primary purpose of providing high-performance,
reusable features for client-server architecture, such as logging,
tracing, inter-process communication (IPC), and polling.

libqb is not intended to be an all-encompassing library, but instead provide
focused APIs that are highly tuned for maximum performance for client-server
applications.

[![Build Status](https://travis-ci.org/ClusterLabs/libqb.png)](https://travis-ci.org/ClusterLabs/libqb)
[![COPR Build Status](https://copr.fedorainfracloud.org/coprs/g/ClusterLabs/devel/package/libqb/status_image/last_build.png)](https://copr.fedorainfracloud.org/coprs/g/ClusterLabs/devel/package/libqb/)

## For more information, see:
* [libqb wiki](https://github.com/clusterlabs/libqb/wiki)
* [Issues/Bugs](https://github.com/clusterlabs/libqb/issues)
* [The doxygen generated manual](http://clusterlabs.github.io/libqb/CURRENT/doxygen/)
* You can build it yourself with the following commands:

    $ make doxygen
    $ firefox ./doc/html/index.html

## Dependencies
* glib-2.0-devel (If you want to build the glib example code)
* check-devel (If you want to run the tests)
* doxygen and graphviz (If you want to build the doxygen man pages or html manual)

## Source Control (GIT)

    git clone git://github.com/ClusterLabs/libqb.git

[See Github](https://github.com/clusterlabs/libqb)

## Installing from source

    $ ./autogen.sh
    $ ./configure
    $ make
    $ sudo make install

## How you can help
If you find this project useful, you may want to consider supporting its future development.
There are a number of ways to support the project.

* Test and report issues.
* Help others on the [developers@clusterlabs.org mailing list](http://clusterlabs.org/mailman/listinfo/developers).
* Contribute documentation, examples and test cases.
* Contribute patches.
* Spread the word.
