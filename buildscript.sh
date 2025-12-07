mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DUSE_ASIO=ON
ninja
ln -sf build/compile_commands.json ../
