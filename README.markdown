# libqb

## What is libqb?
libqb is a library with the primary purpose of providing high performance
client server reusable features. It provides high performance logging,
tracing, ipc, and poll.
We don't intend be an all encompassing library, but instead provide very
specially focused APIs that are highly tuned for maximum performance for client/server applications.

## For more information look at:
* [Our wiki](http://libqb.org)
* [The doxygen generated manual](http://libqb.org/html/doxygen/)
* You can build it yourself with the following commands:

    $ make doxygen
    $ firefox ./doc/html/index.html

## Dependencies
* glib-2.0-devel (If you want to build the glib example code)
* check-devel (If you want to run the tests)
* doxygen and graphviz (If you want to build the doxygen man pages or html manual)

## Installing from source

    $ ./autogen.sh
    $ ./configure
    $ make
    $ sudo make install

