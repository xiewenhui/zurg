#!/bin/sh

basepath=$(cd `dirname $0`; pwd)

mkdir -p $basepath/../third64

cd $basepath/gflags && sh build.sh
cd $basepath/glog && sh build.sh
cd $basepath/gtest && sh build.sh  
cd $basepath/protobuf && sh build.sh  
cd $basepath/tcmalloc && sh build.sh  

cd $basepath/boost && sh build.sh
cd $basepath/libcurl && sh build.sh  
cd $basepath/libunwind && sh build.sh  
cd $basepath/libevent && sh build.sh  
cd $basepath/muduo && sh build.sh  

cd $basepath/snappy && sh build.sh  
cd $basepath/zlib && sh build.sh  
cd $basepath/sofa-pbrpc && sh build.sh


