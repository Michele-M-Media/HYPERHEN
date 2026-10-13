#!/bin/bash

version=$(git describe --tags --always --dirty 2>/dev/null)
if [ ! -e ".git" ] || [ -z $version ]
then
   version=$(grep "^VERSION=" makefile_include.mk | sed "s/.*=//")
fi
echo "Testing version:" $version
#grep "VERSION=" makefile | perl -e "@a = split('=', <>); print @a[1];"`

# get uname
echo "uname="`uname -a`

# get gcc name
if [ -z ${CC} ]
then
   CC="gcc"
fi
echo "${CC}="`${CC} -dumpversion`
echo

# ref:         
# git commit:  b96e96cf8b22a931e8e91098ac37bc72f9e2f033
# commit time: 2023-08-23 14:37:18 +0200
