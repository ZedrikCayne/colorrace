pushd crankshaft
make lib
popd
pushd build
rm colorrace
cmake --build .
popd
