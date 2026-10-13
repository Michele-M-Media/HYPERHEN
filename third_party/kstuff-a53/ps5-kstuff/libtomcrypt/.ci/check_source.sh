#!/bin/bash

# output version
bash .ci/printinfo.sh

make clean > /dev/null

echo "checking..."
./helper.pl --check-all || exit 1

exit 0

# ref:         
# git commit:  b96e96cf8b22a931e8e91098ac37bc72f9e2f033
# commit time: 2023-08-23 14:37:18 +0200
