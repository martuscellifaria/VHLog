# VHLog
VHLog is a lightweight alternative to other C++ Async Log libraries.
It is named after Vladimir Herzog, a brazilian journalist murdered by the military dictatorship in the year of 1975.

### Cloning VHLog
VHLog uses asio as third party dependency for TCP sink support, so you will have to clone the repository with the following command:
```bash
$ git clone --recurse-submodules https://github.com/martuscellifaria/VHLog.git
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
The available log levels are DEBUG, INFO, WARNING, ERROR, FATAL. There is not yet a possibility of filtering out, but it will be implemented soon.

### Next steps
- Add other log sinks (UDP, SysLog...).
