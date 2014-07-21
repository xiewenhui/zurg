NAME=`ls *.gz *.bz2 2>/dev/null`
echo build $NAME
#mysql ��װ��·��, ���Ը�����Ҫ�����޸�
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
#����configure����
#��װ��$INSTALL, ����������������������������, 
CONFIG="./configure --prefix=$INSTALL"
echo $CONFIG
CFLAGS="-fPIC -O" $CONFIG
#���벢��װ
make; make install

cd $INSTALL/..

#������������,�������Ҫ�����Ľ���ŵ���ǰĿ¼�µ�output�м���
rm -rf output
mkdir output
mkdir output/include
mkdir output/lib
cp -r $INSTALL/include/* output/include
cp $INSTALL/lib/*.a output/lib
cp $NAME output

rm -rf ../../third64/zlib
mv output ../../third64/zlib

if which tree; then
	tree ../../third64/zlib
fi
                                 


