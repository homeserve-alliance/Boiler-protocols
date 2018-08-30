#!/bin/sh
version=`head -n 1 ../../VERSION`
revision=`git describe --always`
echo "ebusd=${version},${revision}" > versions.txt
files=`find config/ -type f -or -type l`
../../src/lib/ebus/test/test_filereader $files|sed -e 's#^config/##' -e 's#^\([^ ]*\) #\1=#' -e 's# #,#g'|sort >> versions.txt
