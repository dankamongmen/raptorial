RAPTORIAL
=========
by Nick Black (nick.black@sprezzatech.com)

![image](doc/raptorial.jpg)

Raptorial \Rap*to"ri*al\ (r[a^]p*t[=o]"r[i^]*al), a. (Zool.)
* (a) Rapacious; living upon prey
* (b) Adapted for seizing prey
(from The Collaborative International Dictionary of English v.0.48)

Raptorial is a backwards-compatible, drop-in replacement for a collection of
tools making up the "APT ecosystem." By unifying a number of tools in one code
base, it is hoped that performance, documentation, and testing will be
improved, and that a saner interface to APT actions will be presented to
developers. An emphasis will be put on making effective use of parallel
resources, whether they be disks or CPUs.

Components include, or will include:

* libraptorial. A redesigned interface around APT facilitating rich,
	high-performance client applications.
* rapt-show-versions. A drop-in, high-performance replacement for
	apt-show-versions, making use of libraptorial.
* librapt. A drop-in, high-performance replacement for libapt, implemented
	as a wrapper around libraptorial.
* rapt-get. A drop-in, high-performance replacement for apt-get, making use
	of libraptorial.
* raptorial-file. A drop-in, high-performance replacement for apt-file,
	making use of libraptorial.
* raptitude. An ncurses-based package manager, similar in spirit to (but not
	a drop-in replacement for) aptitude.

It will also be possible to compile and link tools such as apt-get and aptitude
against librapt (and indeed this is regularly done to test compatibility). The
maximum performance, however, will be seen by using raptorial-native tools such
as rapt-get and raptorial-file, since certain design choices in libapt do not
allow robust, safe, high-performance operation.

# Requirements

Required components ought be detected or explicitly not detected by the
Autotools configure script. You'll need:

* Libblossom (https://github.com/dankamongmen/libblossom)
* A C compiler
* POSIX threads
* GNU Autotools (only if building from a git checkout)

Raptorial ought build on any platform capable of running libblossom, which
(right now) means just about any POSIX platform.

# Building

If you're using a git checkout, run 'autoreconf -fis'. There's no need to do
this for a release tarball. This will require the GNU Autotools.

Run ./configure && make && make install, seasoned to taste.

# Differences from standard tools

## rapt-show-versions...

* neither requires nor makes use of the apt-show-versions
  cache. The -i/--initialize option is neither required nor supported.
* does not support the -p/--package option, as this was never
  necessary to use. Simply provide a package specification as an argument.
* does not support the -r/--regex nor -R/--regex-all options.
  Simply provide a regular expression to use it for search.
* does not support the -v/--verbose option. It does not appear to work in
  apt-show-versions anyway.

These options might be added for backwards compatibility, but there are no
plans to do so currently.

# Design

## Threading

Significant effort has gone into making Raptorial perform well on a wide
variety of machines. Even on a single core, Raptorial's use of threads can
improve performance (relative to an unthreaded, synchronous I/O system), due to
disk requests being scheduled earlier. The libblossom library is used to
automatically scale to various architectures. Generally, Raptorial will not
have more threads ready to run than there are processors in the system.

## List lexing

If we need data from both the status file and the package lists, we lex the
status file first, to provide a set of anchors with which we can associate the
package list elements. The two are serialized so that package list lexing
needn't lock this common data structure while searching in it. The package
lists within a directory are lexed in parallel, and the lists themselves will
ideally also be chunked and lexed in parallel (they're not right now,
because libblossom doesn't allow hierarchal blossoms. See [bug #698][b698]).

## Filtered list lexing

Whenever we lex a list (status or package), we can accept a DFA to walk whilst
lexing. In this case, we do not make an entry unless there's already one in the
DFA. This not only saves us allocations and copies, but more importantly it
reduces the amount to search later, since uninteresting elements aren't
present.


[b698]: https://www.sprezzatech.com/bugs/show_bug.cgi?id=698


# Similar projects

## APT2
* http://wiki.debian.org/Apt2
* Started work years ago, doesn't appear to be going anywhere

## libept
* http://web.mornfall.net/libept.html
* Used by debtags, packagesearch, aptitude, synaptic, and goplay
* Uses libapt-pkg and libxapian

## libapt-front
* http://libapt-front.alioth.debian.org/
* Superseded by libept