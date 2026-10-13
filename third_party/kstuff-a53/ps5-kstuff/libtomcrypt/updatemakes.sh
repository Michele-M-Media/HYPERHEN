#!/bin/bash

./helper.pl --update-makefiles || exit 1

makefiles=(makefile makefile_include.mk makefile.shared makefile.unix makefile.mingw makefile.msvc)
vcproj=(libtomcrypt_VS2008.vcproj)

if [ $# -eq 1 ] && [ "$1" == "-c" ]; then
  git add ${makefiles[@]} ${vcproj[@]} doc/Doxyfile && git commit -m 'Update makefiles'
fi

exit 0

# ref:         
# git commit:  b96e96cf8b22a931e8e91098ac37bc72f9e2f033
# commit time: 2023-08-23 14:37:18 +0200
