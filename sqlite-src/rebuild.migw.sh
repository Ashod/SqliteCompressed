#!/bin/sh
#
# This script is used to configure and build SQLite on MingW.
#

TCLDIR=/c/Tools/Tcl/lib/tcl8.5

make distclean

./configure --with-tcl=$TCLDIR LDFLAGS=-L.

make clean
make all
make dll
#make compresstest
make test