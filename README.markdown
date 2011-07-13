# libqb

## What is libqb?
libqb is a library with the primary purpose of providing high performance
client server reusable features. It provides high performance logging,
tracing, ipc, and poll.

We don't intend be an all encompassing library, but instead provide very
specially focused APIs that are highly tuned for maximum performance for client/server applications.

## For more information look at:
* [Our wiki](https://github.com/asalkeld/libqb/wiki)
* [Issues/Bugs](https://github.com/asalkeld/libqb/issues)
* [The doxygen generated manual](http://libqb.org/html/doxygen/)
* You can build it yourself with the following commands:

    $ make doxygen
    $ firefox ./doc/html/index.html

## Dependencies
* glib-2.0-devel (If you want to build the glib example code)
* check-devel (If you want to run the tests)
* doxygen and graphviz (If you want to build the doxygen man pages or html manual)

## Source Control (GIT)

    git clone git://github.com/asalkeld/libqb.git

[See Github](https://github.com/asalkeld/libqb)

## Installing from source

    $ ./autogen.sh
    $ ./configure
    $ make
    $ sudo make install

## How you can help
If you find this project useful, you may want to consider supporting its future development.
There are a number of ways to support the project.

* Test and report issues.
* Help others on the [mailing list](https://fedorahosted.org/mailman/listinfo/quarterback-devel).
* Contribute documentation, examples and test cases.
* Contribute patches.
* Spread the word.

