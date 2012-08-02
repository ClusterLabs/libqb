# grueni/libqb

## What is grueni/libqb?
This site contains informations about compiling libqb for Solaris 11 and OpenIndiana.

## For more information about libqb look at:
* [wiki of libqb](https://github.com/asalkeld/libqb/wiki)

## Dependencies
* Solaris 11U7
	csw
	gcc 4.5

## Source Control (GIT)

    git clone https://github.com/grueni/libqb.git

[See Github](https://github.com/grueni/libqb)

## Prepare the build system for Solaris
Install additional packages from the Solaris repository. The following command install all packages necessary to compile the complete Corosync suite not only Libqb.

	$ pkg install autoconf automake-110 gnu-m4 gettext system/header header-math libnet \
	$    ipmitool gnu-make developer/build/libtool library/libtool/libltdl \
	$    library/security/nss library/ncurses library/security/openssl text/gnu-gettext \
	$    system/io/infiniband/reliable-datagram-sockets-v3 library/gnutls \
	$    developer/versioning/mercurial git \
	$    data/docbook/docbook-style-xsl data/docbook/docbook-style-dsssl /data/docbook/docbook-dtds

Install [OpenCSW] (http://www.opencsw.org/manual/for-administrators/getting-started.html). You should install at least the following packages:     

	$ CSWalternatives
	$ CSWaugeas
	$ CSWbdb48
	$ CSWcacertificates
	$ CSWcas-initsmf
	$ CSWcas-migrateconf
	$ CSWcas-preserveconf
	$ CSWcas-texinfo
	$ CSWcas-usergroup
	$ CSWcommon
	$ CSWcoreutils
	$ CSWfacter
	$ CSWggettext-data
	$ CSWiconv
	$ CSWlibcharset1
	$ CSWlibgcc-s1
	$ CSWlibgdbm3
	$ CSWlibgmp10
	$ CSWlibgnugetopt0
	$ CSWlibhistory4
	$ CSWlibhistory5
	$ CSWlibhistory6
	$ CSWlibiconv2
	$ CSWlibintl8
	$ CSWlibncurses5
	$ CSWlibncursesw5
	$ CSWlibpanel5
	$ CSWlibpanelw5
	$ CSWlibreadline4
	$ CSWlibreadline5
	$ CSWlibreadline6
	$ CSWlibruby18-1
	$ CSWlibssl0-9-8
	$ CSWlibxml2-2
	$ CSWlibz1
	$ CSWncurses
	$ CSWosslrt
	$ CSWpkgutil
	$ CSWreadline
	$ CSWruby18
	$ CSWruby18-dev
	$ CSWrubyaugeas
	$ CSWrubygems
	$ CSWterminfo
	$ CSWzlib
	
## Environment for compiling
We install Libqb in /opt/ha. Change the PREFIX if you would like to use another location.

	$ export CFLAGS='-D__EXTENSIONS__ -D_POSIX_PTHREAD_SEMANTICS -DNAME_MAX=255 -DHOST_NAME_MAX=255 -I/usr/include -I/opt/ha/include'
	$ export LDFLAGS='-R/usr/gnu/lib -L/opt/ha/lib -L/usr/gnu/lib -L/lib -L/usr/lib'
	$ export PREFIX=/opt/ha
	$ export PATH=/opt/csw/bin:/usr/gnu/bin:/usr/bin:/usr/sbin:$PREFIX/bin
	$ mkdir -p $PREFIX

## Packages	to compile

* [libESMTP] (http://www.stafford.uklinux.net/libesmtp/)
* [Check] (http://check.sourceforge.net/)
* [Reusable Cluster Components] (http://hg.linux-ha.org/glue)
* The [resource-agents](https://github.com/ClusterLabs/resource-agents) from [ClusterLabs](http://www.clusterlabs.org/)
* [libqb](https://github.com/asalkeld/libqb)
* [libstatgrab](http://dl.ambiweb.de/mirrors/ftp.i-scream.org/libstatgrab/)
* [Corosync](http://www.corosync.org/doku.php?id=welcome)
* [Heartbeat](http://hg.linux-ha.org/heartbeat-STABLE_3_0)
* [Pacemaker](https://github.com/beekhof/pacemaker)
* [crmsh](http://savannah.nongnu.org/projects/crmsh/)
	
## Compile libesmtp

	$ export PKG_CONFIG_PATH='/opt/ha/lib/pkgconfig:/usr/lib/pkgconfig/'
	$ export CFLAGS='-std=c89 -D__EXTENSIONS__ -DNAME_MAX=255 -DHOST_NAME_MAX=255'
	$ wget http://www.stafford.uklinux.net/libesmtp/libesmtp-1.0.6.tar.gz
	$ gtar zxvf libesmtp-1.0.6.tar.gz
	$ cd libesmtp-1.0.6
	$ mkdir m4
	$ autoreconf -i
	$ ./configure --prefix=$PREFIX 
	$ perl -pi -e 's#// TODO: handle GEN_IPADD##' smtp-tls.c
	$ gmake
	$ gmake install 
	$ cp auth-client.h /opt/ha/include
	$ cp auth-plugin.h /opt/ha/include
	$ cp libesmtp.h    /opt/ha/include

## How you can help
If you find this project useful, you may want to consider supporting its future development.
There are a number of ways to support the project.

* Test and report issues.
* Help others on the [mailing list](https://fedorahosted.org/mailman/listinfo/quarterback-devel).
* Contribute documentation, examples and test cases.
* Contribute patches.
* Spread the word.

