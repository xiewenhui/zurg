#!/bin/sh

set -x

SOURCE_DIR=`pwd`
BUILD_DIR=${BUILD_DIR:-./build}
BUILD_TYPE=${BUILD_TYPE:-debug}
INSTALL_DIR=${INSTALL_DIR:-./${BUILD_TYPE}-install}
BUILD_NO_EXAMPLES=${BUILD_NO_EXAMPLES:-0}

mkdir -p $BUILD_DIR/$BUILD_TYPE \
  && cd $BUILD_DIR/$BUILD_TYPE \
  && cmake \
           -DBOOST_INCLUDEDIR=$SOURCE_DIR/../../third64/boost/include \
           -DBoost_INCLUDE_DIRS=$SOURCE_DIR/../../third64/boost/include \
           -DBOOST_LIBRARYDIR=$SOURCE_DIR/../../third64/boost/lib \
           -DTCMALLOC_INCLUDE_DIR=$SOURCE_DIR/../../third64/tcmalloc/include/ \
           -DTCMALLOC_LIBRARY=$SOURCE_DIR/../../third64/tcmalloc/lib/ \
           -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
           -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
           -DCMAKE_BUILD_NO_EXAMPLES=$BUILD_NO_EXAMPLES \
           $SOURCE_DIR \
  && make $*

rm -rf $SOURCE_DIR/output
cd $SOURCE_DIR/build/debug/ && make install && mv debug-install ../../output
cd $SOURCE_DIR

rm -rf $SOURCE_DIR/../../third64/muduo
mv $SOURCE_DIR/output $SOURCE_DIR/../../third64/muduo

if which tree; then
    tree $SOURCE_DIR/../../third64/muduo
fi

# cd $SOURCE_DIR && doxygen

