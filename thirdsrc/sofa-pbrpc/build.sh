make -j 8
make install

rm -rf ../../third64/sofa-pbrpc
mv output ../../third64/sofa-pbrpc

if which tree; then
    tree ../../third64/sofa-pbrpc
fi
