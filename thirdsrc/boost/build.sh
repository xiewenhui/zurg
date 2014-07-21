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
sed -i -e 's/\(<toolset>gcc:<cxxflags>-Wno-variadic-macros\)/#\1/' libs/chrono/build/Jamfile.v2
sed -i -e 's/\(<toolset>gcc:<cxxflags>-Wno-variadic-macros\)/#\1/' libs/thread/build/Jamfile.v2
#����bjam
rm -rf bjam
sh bootstrap.sh
#���벢�Ұ�װ
./bjam --prefix=$INSTALL --threading=multi --link=static cxxflags='-fPIC -O2' cflags='-fPIC -O2' install
./bjam --prefix=$INSTALL --threading=multi --link=static --with-thread --with-chrono cxxflags='-fPIC -O2' cflags='-fPIC -O2' install
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
