# VHLog
VHLog is a lightweight alternative to other C++ Async Log libraries.
It is named after Vladimir Herzog, a brazilian journalist murdered by the military dictatorship in the year of 1975.

### Cloning VHLog
You can clone the repository as usual with the following command:
```bash
$ git clone https://github.com/martuscellifaria/VHLog.git
```

However, if you need to activate the TCP sink option, you can clone it with submodules: 
```bash
$ git clone -recurse-submodules https://github.com/martuscellifaria/VHLog.git
```

If you cloned with the first option, you can later use the following command:
```bash
$ git sumodule update --init
```

### Embedding VHLog on your project
You can embed VHLog in your project just like any other C++ library. Just setup your build system to the include VHLog.h and VHLog.cpp files, with the third_party directory as well (you can check the CMakeLists.txt provided on the root of this project as well), and just place #include "VHLog.h" on top of the file you desire.
You can take a look at examples/HelloVHLog.cpp.

### Build the example
In order to build the example, if you are on Linux and have ninja installed, you can follow the buildscript provided.
```bash
$ sh buildscript.sh
```

Alternatively, you can also use cmake and make:
```bash
$ mkdir build
$ cd build
$ cmake ..
$ make
```

You may also be able to produce a Visual Studio Solution (.sln) on Windows following the steps:
```powershell
$ mkdir build
$ cd build
$ cmake ..
```

### TCP Sink compile option
If you want to enable support for TCP Log Sink, you will have to activate the asio compilation. First ensure you have the asio library inside the third_party directory. Then you can set the following flag on your build with cmake:
```bash
$ cmake .. -DUSE_ASIO=ON
```

You can take the buildscript.sh file as example, there is the flag for enabling compilation with asio disabled by default.
```bash
$ cat buildscript.sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DUSE_ASIO=OFF
ninja
ln -sf build/compile_commands.json ../
```

### Supported platforms
Linux, Windows, MacOS

### Usage
You have to instantiate VHLog at first, add a sink, and then you should be ready.
```c++

#include "../include/VHLog.h"

int main() {
    VHLogger vladoLog = VHLogger(false); //The boolean here filters the logging from DEBUGLV out (for production).
    vladoLog.addConsoleSink();
    vladoLog.log(VHLogLevel::INFOLV, "Hi VHLog!");
}
```

### Available sinks
Up to this point, VHLog has a console sink, a rotating file sink, a TCP sink and a null sink. Multi-sink is also possible, if you call addLogSink multiple times.

### Log levels
The available log levels are DEBUG, INFO, WARNING, ERROR, FATAL. Debug level messages can be filtered out by passing a boolean with value false onto the VHLogger constructor. The default constructor has it set to true, so debug level messages are active by default.

### Next steps
- Asio/TCP sink as optional, so one do not has to have to clone with recurse-submodules to use VHLog.
- Add other log sinks (UDP, SysLog...).
