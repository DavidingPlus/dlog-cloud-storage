# dlog-cloud-storage

基于 C++17 的轻量级云存储系统，集成 dlog 异步日志库，支持 Web 文件上传下载、普通存储和压缩存储。

## 单元测试

`bundle`、`cpp-base64` 和官方 xmake-repo 的 `jsoncpp` 由 XMake 下载和链接，沿用项目当前构建模式的动态库配置。启用 GoogleTest 后运行：

```bash
xmake f -m debug --with_gtest=yes -y
xmake test
```

通用工具位于 `src/utils/`，使用 `utils` 命名空间，构建为各平台共用的内部静态库。对应测试按源码拆分为 `test/utils/common.cpp`、`test/utils/fileutil.cpp` 和 `test/utils/jsonutil.cpp`，覆盖可执行文件绝对路径及切换工作目录、十六进制和 URL 解码、文件属性与二进制读写、目录和符号链接、压缩解压、JSON 转换及错误处理。文件测试使用独立临时目录并自动清理；权限用例在当前用户能够绕过权限限制时跳过；符号链接、设备或跨根路径相关用例在环境不支持时跳过。`JsonUtil` 测试沿用接口当前的 JsonCpp 默认解析配置。

`utils::executableFullPath()` 和 `utils::executableDirectory()` 返回 `std::filesystem::path`，分别表示当前进程可执行文件的绝对路径和所在目录，不受当前工作目录影响。支持 Windows 和 Linux，可通过 `utils::executableDirectory() / "config" / "server.conf"` 定位运行配置。

第三方库测试位于 `test/third_party/`。JsonCpp 测试覆盖值与容器操作、解析和序列化、流接口、Unicode 与空字节、64 位数值边界及大批量元数据往返。错误输入测试显式使用严格模式，检查语法错误、重复键、多余内容、非标准扩展和嵌套深度限制。

## 服务端打包（Linux）

使用 XMake 内置的 XPack 生成二进制发布包：

```bash
xmake f -m release
xmake pack
```

`xmake pack` 会自动编译服务端，并收集配置文件和实际需要的第三方动态库，无需先执行 `xmake install`。发布包不包含内部静态库、测试和开发示例。

默认输出到 `build/xpack/dlog-cloud-storage-server/`，文件名包含平台、架构、构建模式和版本，例如：

```text
dlog-cloud-storage-server-linux-x86_64-release-v0.0.0.tar.gz
```

解压后，进入 `bin/` 目录，按需修改其中的 `config/server.conf`，再执行 `./dlog-cloud-storage-server`。第三方动态库位于 `lib/`，程序通过相对自身位置的 RPATH 查找它们，可以整体移动解压目录。没有实际使用第三方动态库时，发布包可能只有 `bin/`。

`$ORIGIN/../lib` 在链接阶段直接写入程序，并使用 RPATH 支持间接动态库依赖；安装和打包阶段保留该设置，不需要额外的 ELF 修改工具。开发时使用 `xmake run dlog-cloud-storage-server`，由 XMake 提供依赖包的运行环境。修改项目中的 `config/server.conf` 后，执行 `xmake config`，配置阶段会将文件同步到可执行文件目录下的 `config/server.conf`，无需先编译服务端。

系统基础库（如 glibc）由目标系统提供，发布环境仍需匹配 CPU 架构和系统运行库版本。

可通过 `xmake pack -o <目录>` 指定输出目录；已编译后可用 `xmake pack --autobuild=n` 跳过构建。

