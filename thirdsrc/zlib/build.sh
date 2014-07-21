NAME=`ls *.gz *.bz2 2>/dev/null`
echo build $NAME
#mysql 安装的路径, 可以根据需要进行修改
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


#进入目录进行编译安装
cd $SRC
#设置configure参数
#安装到$INSTALL, 可以在这后面继续添加其他编译参数, 
CONFIG="./configure --prefix=$INSTALL"
echo $CONFIG
CFLAGS="-fPIC -O" $CONFIG
#编译并安装
make; make install

cd $INSTALL/..

#继续其他操作,把最后需要发布的结果放到当前目录下的output中即可
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
                                 


