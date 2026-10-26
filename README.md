# dlog-cloud-storage

基于 C++17 的轻量级云存储系统，集成 dlog 异步日志库，支持 Web 文件上传下载、普通存储和压缩存储。

## 内部模块构建

`dlog-cloud-storage-utils-static` 编译 `src/utils/` 下的通用工具，支持 Windows 和 Linux。Linux 下另有 `dlog-cloud-storage-server-static`，编译 `src/server/` 下除 `main.cpp` 外的服务端实现，并依赖工具静态库。服务端可执行目标只编译 `src/server/main.cpp`，链接服务端静态库。

Linux 单元测试复用服务端静态库，其他平台的测试依赖工具静态库。开发示例按需通过 `add_deps("dlog-cloud-storage-utils-static")` 或 Linux 下的 `add_deps("dlog-cloud-storage-server-static")` 引入模块，无需再次添加模块的 `.cpp` 文件；目前只使用工具函数的 `ServerUtilsTest` 仍依赖工具静态库。同一平台、架构和构建配置下，各目标复用这些静态库的编译结果，并分别链接为可执行程序。

两个内部静态库仅用于项目构建，不单独安装或打包。发布服务端时，所需内部实现静态链接到可执行程序中，实际需要的第三方动态库随程序一起打包。

服务端资源随模块放在 `src/server/` 下：`src/server/config/server.conf` 保存运行配置，`src/server/web/template.html` 保存页面模板。XMake 配置阶段将它们分别同步到服务端和测试程序所在目录下的 `config/server.conf`、`web/template.html`；安装和打包后对应 `bin/config/server.conf`、`bin/web/template.html`。运行时通过可执行文件目录定位资源。

## 单元测试

`bundle`、`cpp-base64`、`jsoncpp` 和 `libevent` 由 XMake 下载和链接，沿用项目当前构建模式的动态库配置。启用 GoogleTest 后运行：

```bash
xmake f -m debug --with_gtest=yes -y
xmake test
```

通用工具位于 `src/utils/`，使用 `utils` 命名空间，构建为各平台共用的内部静态库。对应测试按源码拆分为 `test/utils/common_test.cpp`、`test/utils/fileutil_test.cpp` 和 `test/utils/jsonutil_test.cpp`，覆盖可执行文件绝对路径及切换工作目录、十六进制转换、百分号编码、URL 路径与文件名编码、URL 解码、HTML 文本转义、文件属性与二进制读写、目录和符号链接、压缩解压、JSON 转换及错误处理。编码与转义测试覆盖全部 256 个字节、斜杠策略、UTF-8、内嵌空字节、已有编码或实体、长文本，以及编码和解码往返；百分号编码使用独立的固定期望值，避免只检查往返时漏掉双方共同的错误。文件测试使用独立临时目录并自动清理；权限用例在当前用户能够绕过权限限制时跳过；符号链接、设备或跨根路径相关用例在环境不支持时跳过。`JsonUtil` 测试沿用接口当前的 JsonCpp 默认解析配置。

`utils::executableFullPath()` 和 `utils::executableDirectory()` 返回 `std::filesystem::path`，分别表示当前进程可执行文件的绝对路径和所在目录，不受当前工作目录影响。支持 Windows 和 Linux，可通过 `utils::executableDirectory() / "config" / "server.conf"` 定位运行配置。

Linux 服务端的 `Config::readConfig()` 从可执行文件目录下的 `config/server.conf` 读取配置，不受工作目录影响。赋值前检查根节点、必填字符串、端口范围、压缩格式和下载前缀，校验失败时抛出 `std::invalid_argument`；文件读取和 JSON 解析失败时沿用工具类异常。对应测试位于 `test/server/config_test.cpp`，可用 `xmake test tests/server` 执行。

配置中的 `compressed_files_dir`、`uncompressed_files_dir` 和 `metadata_file` 分别指定压缩文件目录、未经服务端压缩的文件目录和元数据持久化文件。默认路径为 `./data/compressed/`、`./data/uncompressed/` 和 `./data/metadata.json`。`Config` 构造时通过 `readConfig()` 将相对路径按可执行文件所在目录解析为绝对路径，再对相对路径解析结果和显式配置的绝对路径统一调用 `lexically_normal()`，以 `std::filesystem::path` 保存和返回；再次读取配置时沿用相同规则。`DataManager` 通过 `getMetadataFilePath()` 获取元数据持久化文件路径。`download_prefix` 保持为路由字符串。

`FileMetadata::FromFile()` 生成的绝对路径也经过词法标准化。`DataManager` 在加载历史 JSON、插入和更新元数据时统一标准化文件路径；按路径查询时，先以可执行文件目录解析为绝对路径，再标准化后比较，因此 `.`、`..` 等不同写法可以匹配同一条记录。查询不要求文件存在，不解析符号链接；加载时只标准化内存中的路径，后续持久化时才写出标准化后的路径。

Config 测试覆盖首次构造与单例复用、七个字段读取和重载、工作目录中存在另一份配置时的定位、文件缺失和目录占位、读取权限、JSON 错误、逐字段缺失及类型错误、32/64 位数值越界、端口边界、全部支持的压缩格式、下载前缀、Unicode 与路径解析、切换工作目录后路径保持不变、额外字段，以及各种失败后的旧值保留和恢复加载。首次构造及失败重试通过 GTest 的 `threadsafe` 进程测试执行，保证单例尚未初始化，且不依赖测试顺序。测试会恢复原配置、文件权限和工作目录，并清理临时文件；当前用户能绕过读取权限时跳过对应权限用例。数值类型沿用 JsonCpp 的 `isInt()` 语义，`8081.0` 等能表示为 `int` 的整数值也会被接受。

`FileMetadata` 的基础测试位于 `test/server/datamanager_test.cpp`，通过 `xmake test tests/server` 执行，检查正常文件的元数据字段及文件不存在时的异常。

第三方库测试位于 `test/third_party/`。JsonCpp 测试覆盖值与容器操作、解析和序列化、流接口、Unicode 与空字节、64 位数值边界及大批量元数据往返。错误输入测试显式使用严格模式，检查语法错误、重复键、多余内容、非标准扩展和嵌套深度限制。

libevent 测试位于 `test/third_party/libevent_test.cpp`，通过 `rtk xmake test -y tests/third_party` 执行。覆盖事件激活、取消与释放、一次性和重复定时器、优先级、事件循环退出与恢复、二进制缓冲区、URI 和查询参数解析，以及 HTTP 请求头和输入输出缓冲区。用例不监听端口、不访问外部网络、不发送进程信号；定时器测试不依赖精确耗时断言，重复定时器设置退出期限，Windows 下由测试套件初始化并清理 Winsock。

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

`$ORIGIN/../lib` 在链接阶段直接写入程序，并使用 RPATH 支持间接动态库依赖；安装和打包阶段保留该设置，不需要额外的 ELF 修改工具。开发时使用 `xmake run dlog-cloud-storage-server`，由 XMake 提供依赖包的运行环境。修改 `src/server/config/server.conf` 或 `src/server/web/template.html` 后，执行 `xmake config`，配置阶段会将文件同步到可执行文件目录下的 `config/server.conf`、`web/template.html`，无需先编译服务端。

系统基础库（如 glibc）由目标系统提供，发布环境仍需匹配 CPU 架构和系统运行库版本。

可通过 `xmake pack -o <目录>` 指定输出目录；已编译后可用 `xmake pack --autobuild=n` 跳过构建。

