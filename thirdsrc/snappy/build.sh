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
#����
CONFIG="./configure --prefix=$INSTALL --enable-shared=no --enable-static=yes"
CFLAGS="-fPIC -O2 -g" CXXFLAGS="-fPIC -O2 -g" $CONFIG
#���벢��װ
make -j 8
make install

cd $INSTALL/..

#������������,�������Ҫ�����Ľ���ŵ���ǰĿ¼�µ�output�м���
rm -rf output
mkdir output
cp -r $INSTALL/include output
cp -r $INSTALL/lib output
#ɾ��lib�����.a�ļ�
find output/lib -type f ! -iname "*.a" -exec rm -rf {} \;
#ɾ��lib�����Ŀ¼
find output/lib -type d -empty | xargs rm -rf 
#ɾ��lib������������ļ�
find output/lib -type l -exec rm -rf {} \;

rm -rf ../../third64/snappy
mv output ../../third64/snappy

if which tree; then
    tree ../../third64/snappy
fi