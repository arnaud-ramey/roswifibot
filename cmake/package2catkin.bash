#!/bin/bash
# Convert the package into a catkin-compliant package

# get script path - https://stackoverflow.com/questions/4774054/reliable-way-for-a-bash-script-to-get-the-full-path-to-itself
pushd `dirname $0` > /dev/null
SCRIPTPATH=`pwd`
popd > /dev/null

cd ..
rm -f manifest.xml Makefile # clean for rosmake files
ln -s --force $SCRIPTPATH/CMakeLists_catkin.txt  CMakeLists.txt
ln -s --force $SCRIPTPATH/package.xml
ls -al
rospack profile
