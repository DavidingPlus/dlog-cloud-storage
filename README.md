# dlog-cloud-storage

基于 C++17 的轻量级云存储系统，集成 dlog 异步日志库，支持 Web 文件上传下载、普通存储和压缩存储。

## 服务端打包（Linux）

使用 XMake 内置的 XPack 生成二进制发布包：

```bash
xmake f -m release
xmake pack
```

`xmake pack` 会自动编译服务端，并收集实际需要的第三方动态库，无需先执行 `xmake install`。发布包不包含内部静态库、测试和开发示例。

默认输出到 `build/xpack/dlog-cloud-storage-server/`，文件名包含平台、架构、构建模式和版本，例如：

```text
dlog-cloud-storage-server-linux-x86_64-release-v0.0.0.tar.gz
```

解压后，可直接执行 `bin/dlog-cloud-storage-server`。第三方动态库位于 `lib/`，程序通过相对自身位置的 RPATH 查找它们，可以整体移动解压目录。没有实际使用第三方动态库时，发布包可能只有 `bin/`。

`$ORIGIN/../lib` 在链接阶段直接写入程序，并使用 RPATH 支持间接动态库依赖；安装和打包阶段保留该设置，不需要额外的 ELF 修改工具。开发时使用 `xmake run dlog-cloud-storage-server`，由 XMake 提供依赖包的运行环境。

系统基础库（如 glibc）由目标系统提供，发布环境仍需匹配 CPU 架构和系统运行库版本。

可通过 `xmake pack -o <目录>` 指定输出目录；已编译后可用 `xmake pack --autobuild=n` 跳过构建。

