#!/bin/sh
set -e

if test -e .cvsignore ; then : ; else cp .cvsignore-suggested .cvsignore ; fi

autoreconf -i

./configure --enable-maintainer-mode CFLAGS="-Wall -g -O2 -Wmissing-prototypes -Wstrict-prototypes -Wshadow"
