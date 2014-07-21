NAME=`ls *.gz *.bz2 *.zip 2>/dev/null`
echo build $NAME
#��װ��·��, ���Ը�����Ҫ�����޸�
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

#����Ŀ¼���б��밲װ
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

#ɾ��lib�����.a�ļ�
find output/lib -type f ! -iname "*.a" -exec rm -rf {} \;
#ɾ��lib�����Ŀ¼
find output/lib -type d -empty | xargs rm -rf 
#ɾ��lib������������ļ�
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
