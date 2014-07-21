NAME=`ls *.gz *.bz2 *.zip 2>/dev/null`
echo build $NAME
#安装的路径, 可以根据需要进行修改
INSTALL=$PWD/install

case $NAME in
    *.tar.gz)
        SRC=`basename $NAME .tar.gz`
        rm -rf $SRC
        tar xzf $NAME
        ;;
    *.tar.bz2)
        SRC=`basename $NAME .tar.bz2`
        rm -rf $SRC
        tar xjf $NAME
        ;;
    *.zip)
        SRC=`basename $NAME .zip`
        rm -rf $SRC
        unzip $NAME
        ;;
esac

#进入目录进行编译安装
cd $SRC

CXXFLAGS=-fPIC CFLAGS=-fPIC ./configure

make -j 12

rm -rf ../output
mkdir ../output/lib -p
mkdir ../output/include -p
cp -r include/* ../output/include
cp lib/.libs/libgtest.a ../output/lib
cp lib/.libs/libgtest_main.a ../output/lib

cd ..

#删除lib下面非.a文件
find output/lib -type f ! -iname "*.a" -exec rm -rf {} \;
#删除lib下面空目录
find output/lib -type d -empty | xargs rm -rf 
#删除lib下面的软链接文件
find output/lib -type l -exec rm -rf {} \;

if test -s README; then
    cp README output/
fi

if test -s ChangeLog; then
    cp ChangeLog output/
fi

rm -rf ../../third64/gtest
mv output ../../third64/gtest

if which tree; then
    tree ../../third64/gtest
fi
