NAME=`ls *.gz *.bz2 2>/dev/null`
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
esac

#进入目录进行编译安装
cd $SRC
sed -i -e 's/\(<toolset>gcc:<cxxflags>-Wno-variadic-macros\)/#\1/' libs/chrono/build/Jamfile.v2
sed -i -e 's/\(<toolset>gcc:<cxxflags>-Wno-variadic-macros\)/#\1/' libs/thread/build/Jamfile.v2
#编译bjam
rm -rf bjam
sh bootstrap.sh
#编译并且安装
./bjam --prefix=$INSTALL --threading=multi --link=static cxxflags='-fPIC -O2' cflags='-fPIC -O2' install
./bjam --prefix=$INSTALL --threading=multi --link=static --with-thread --with-chrono cxxflags='-fPIC -O2' cflags='-fPIC -O2' install
cd $INSTALL/..

#继续其他操作,把最后需要发布的结果放到当前目录下的output中即可
rm -rf output
mkdir output
cp -r $INSTALL/include output
cp -r $INSTALL/lib output
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

rm -rf ../../third64/boost
mv output ../../third64/boost

if which tree; then
    tree ../../third64/boost
fi
