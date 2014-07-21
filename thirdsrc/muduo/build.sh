#!/bin/sh

set -x

SOURCE_DIR=`pwd`
BUILD_DIR=${BUILD_DIR:-./build}
BUILD_TYPE=${BUILD_TYPE:-release}
INSTALL_DIR=${INSTALL_DIR:-./${BUILD_TYPE}-install}
BUILD_NO_EXAMPLES=${BUILD_NO_EXAMPLES:-0}

mkdir -p $BUILD_DIR/$BUILD_TYPE \
  && cd $BUILD_DIR/$BUILD_TYPE \
  && cmake \
           -DBOOST_INCLUDEDIR=../../third64/boost/include \
           -DBOOST_LIBRARYDIR=../../third64/boost/lib \
           -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
           -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
           -DCMAKE_BUILD_NO_EXAMPLES=$BUILD_NO_EXAMPLES \
           $SOURCE_DIR \
  && make $*

rm -rf $SOURCE_DIR/output
cd $SOURCE_DIR/build/release/ && make install && mv release-install ../../output
cd $SOURCE_DIR

rm -rf ../../third64/muduo
mv output ../../third64/muduo

if which tree; then
    tree ../../third64/muduo
fi

# cd $SOURCE_DIR && doxygen

