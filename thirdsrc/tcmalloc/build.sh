NAME=`ls *.gz *.bz2 2>/dev/null`
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
esac

#����Ŀ¼���б��밲װ
cd $SRC

patch -p0 < ../tcmalloc.patch

#����
CONFIG="./configure --prefix=$INSTALL --enable-frame-pointers --enable-shared=yes --enable-static=yes"
CFLAGS="-fPIC -g -O2" CXXFLAGS="-fPIC -g -O2" $CONFIG
#���벢��װ
make -j 8
make install

cd $INSTALL/..

#������������,�������Ҫ�����Ľ���ŵ���ǰĿ¼�µ�output�м���
rm -rf output
mkdir output
cp -r $INSTALL/include output
cp -r $INSTALL/lib output
cp -r $INSTALL/bin output
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

rm -rf ../../third64/tcmalloc
mv output ../../third64/tcmalloc

if which tree; then
    tree ../../third64/tcmalloc
fi