# RAPTORIAL

by Nick Black (dankamongmen@gmail.com)

[![Build Status](https://drone.dsscaw.com:4443/api/badges/dankamongmen/raptorial/status.svg)](https://drone.dsscaw.com:4443/dankamongmen/raptorial)

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
resources, whether they be disks or CPUs. It makes no use of on-disk cached
representations of its primary datastore: all data is present exactly one time
in the filesystem. It requires no tmpfiles, and indeed never writes to disk.

Components include:

* libraptorial. A redesigned interface around APT facilitating rich,
	high-performance client applications.
* rapt-show-versions. A drop-in, high-performance replacement for
	apt-show-versions, making use of libraptorial.
* raptorial-file. A drop-in, high-performance replacement for apt-file,
	making use of libraptorial.
* rapt-parsechangelog: A drop-in, high-performance replacement for
	dpkg-parsechangelog, making use of libraptorial.

I might possibly add:

* librapt. A drop-in, high-performance replacement for libapt, implemented
	as a wrapper around libraptorial.
* rapt-get. A drop-in, high-performance replacement for apt-get, making use
	of libraptorial.
* raptitude. An ncurses-based package manager, similar in spirit to (but not
	a drop-in replacement for) aptitude.

## Requirements

Required components ought be detected or explicitly not detected by the
Autotools configure script. You'll need:

* A C compiler plus CMake
* POSIX threads
* Libblossom (https://github.com/dankamongmen/libblossom)
* zlib (http://www.zlib.net/)

Raptorial ought build on any platform capable of running libblossom, which
(right now) means just about any POSIX platform.

## Building

Run `cmake && make && make install`.

## UI-visible divergences from preexisting APT tools

### Regular expressions / "pattern searches"

* The variations of regular expression admitted by the different tools are
  collapsed to a single, reduced definition. A search pattern is composed of:
	* Legal characters for the range of the search (usually alphanums + '_'),
	* Kleene closure ('*'),
	* Union ('|'),
	* Left bound ('^')
	* Right bound ('$')
	* Nested pairs of parentheses ('(' + ')')

### rapt-show-versions(1) vs apt-show-versions(1)

* neither requires nor makes use of the apt-show-versions(1) cache.
  The -i/--initialize option is neither required nor supported.
* does not support the -p/--package option; it was syntactic sugar.
  Simply provide a package specification as an argument.
* does not support the -r/--regex nor -R/--regex-all options; they were
  syntactic sugar. Simply provide a regular expression to use it for search.
* does not support the -v/--verbose option. It does not appear to work in
  apt-show-versions(1) anyway.

These options might be added for backwards compatibility, but there are no
plans to do so currently.

### raptorial-file(1) vs apt-file(1)

* neither requires nor makes use of the apt-file(1) cache.
  The -c/--cache option is neither required nor supported.
* does not support the -x/--regexp options; they were syntactic sugar. Simply
  provide a regular expression to use it for search.
* $HOME/.cache/apt-file is no longer checked by default for non-root users.
  simply use -c explicitly to use a directory other than the systemwide
  default /var/cache/apt/apt-file. this eliminates a class of pathological
  problems (i.e., partially broken user cache with working system cache).
* -F/--fixed-string is not supported. it only applied to -r/--regex-style
  searches in the old apt-file(1). raptorial-file(1) always matches the content
  text against the search terms for any presence. to require a full match,
  use '^term$'.

### rapt-parsechangelog (1) vs dpkg-parsechangelog

* the -L and -F options (alternative parsers) are not currently supported,
  and probably won't be.

## Design

### Threading

Significant effort has gone into making Raptorial perform well on a wide
variety of machines. Even on a single core, Raptorial's use of threads can
improve performance (relative to an unthreaded, synchronous I/O system), due to
disk requests being scheduled earlier. The libblossom library is used to
automatically scale to various architectures. Generally, Raptorial will not
have more threads ready to run than there are processors in the system.

### List lexing

If we need data from both the status file and the package lists, we lex the
status file first, to provide a set of anchors with which we can associate the
package list elements. The two are serialized so that package list lexing
needn't lock this common data structure while searching in it. The package
lists within a directory are lexed in parallel, and the lists themselves will
ideally also be chunked and lexed in parallel (they're not right now,
because libblossom doesn't allow hierarchal blossoms. See [bug #698][b698]).

[b698]: https://www.sprezzatech.com/bugs/show_bug.cgi?id=698

### Multiple matching

Rather than use a standard string search algorithm, we make use of the
Aho-Corasick automaton. This allows us to match all patterns against a text at
the same time. It is intended that this be replaced by the "advanced" variant
of the algorithm, so that we can benefit from skips following partial prefix
matches.

### Backwards skipping

When we are matching against a single string, there's no benefit from the
somewhat elaborate Aho-Corasick construction. In the case where we are matching
a single string, and that string contains at least two different characters, we
instead use Boyer-Moore (*not* Boyer-Moore-Harspool), to benefit from skips.

### Filtered list lexing

Whenever we lex a list (status or package), we can accept a DFA to walk whilst
lexing. In this case, we do not make an entry unless there's already one in the
DFA. This not only saves us allocations and copies, but more importantly it
reduces the amount to search later, since uninteresting elements aren't
present.

### Pattern searches

Recall that regular languages are equivalent to discrete finite automata.

We admit only the true regular operations (concatenation, alternation, and
Kleene closure). This is strictly less powerful than the PCRE support
approximated more or less by the preexisting tools, but it allows us to
implement regexes purely as edges in our native DFAs. Thankfully, while
backreferences cannot be built out of these regular operations, it seems
unlikely that backrefs would ever be useful in these programs' contexts. By
rejecting syntactic sugar like character classes, atrivial Kleene ('+'),
numeric ranges and match-all-glyphs ('.'), we use only characters which cannot
show up in any of our search ranges, and thus needn't introduce escaping.


## Similar projects

### APT2
* http://wiki.debian.org/Apt2
* Started work years ago, doesn't appear to be going anywhere

### libept
* http://web.mornfall.net/libept.html
* Used by debtags, packagesearch, aptitude, synaptic, and goplay
* Uses libapt-pkg and libxapian

### libapt-front
* http://libapt-front.alioth.debian.org/
* Superseded by libept
