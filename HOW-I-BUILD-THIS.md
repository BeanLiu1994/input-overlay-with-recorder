build with cmake configuration:

```
mkdir build
cd build
cmake .. --preset windows-x64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

提示 Could not find a package configuration file provided by "libobs"

libobs由.deps\obs-studio-31.1.1\libobs中的cmake提供，因此填写为 libobs_DIR=D:\beanpliu\input-overlay\.deps\obs-studio-31.1.1\build_x64\libobs 
w32-pthreads_DIR，obs-frontend-api_DIR，同理。

