make clean 
make

rm -rf ../../third64/muduo-udns
mkdir -p ../../third64/muduo-udns 
cp -r * ../../third64/muduo-udns/

if which tree; then
    tree ../../third64/muduo-udns 
fi
