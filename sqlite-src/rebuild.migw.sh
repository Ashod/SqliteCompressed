#!/bin/sh
#
# This script is used to configure and build SQLite on MingW.
#

TCLDIR=/c/tools/Tcl/lib/tcl8.5

./configure --with-tcl=$TCLDIR

make rebuild
make build_tests