# LibeventTest

Linux 下的最小 HTTP 服务示例，使用项目已声明的 xrepo libevent 依赖。主文件按步骤演示事件循环、HTTP 监听、请求回调、响应缓冲区、信号事件和资源释放，便于对照后续 `Service` 实现。

示例使用 `std::cout`、`std::cerr` 输出信息，使用 `std::string` 保存地址、`std::string_view` 表示请求路径和响应正文，通过带自定义删除器的 `std::unique_ptr` 管理 libevent 资源。在调用 libevent 接口时，再通过 `c_str()` 或 `data()`、`size()` 传递字符串内容。

## 构建与运行

在项目根目录执行。`.buildme` 是现有 snippet 的启用标记，已加入全局忽略规则；新检出项目时需要自行创建。

```bash
rtk touch snippet/LibeventTest/.buildme
rtk xmake build -y LibeventTest
rtk xmake run LibeventTest
```

程序监听 `http://127.0.0.1:8082/`，启动时打印 libevent 版本和事件后端。另开终端访问，也可以直接在浏览器打开该地址：

```bash
rtk curl -i http://127.0.0.1:8082/
rtk curl -i http://127.0.0.1:8082/missing
rtk curl -i -X POST http://127.0.0.1:8082/
```

依次得到 `200` 和 `Hello, libevent!`、`404`、`405`。在运行程序的终端按 Ctrl+C，信号回调会让事件循环返回，随后释放资源。

## 代码中的调用顺序

1. `event_base_new()` 创建事件循环。
2. `evhttp_new()` 创建 HTTP 服务，`evhttp_bind_socket()` 监听地址和端口。
3. `evhttp_set_gencb()` 注册请求回调；回调通过 `evhttp_request_get_command()` 检查请求方法，对 `/` 的非 GET 请求返回 405。
4. `evsignal_new()` 和 `event_add()` 注册 SIGINT 事件；回调的 `arg` 接收注册时传入的 `event_base` 指针。
5. `event_base_dispatch()` 等待并分发事件。请求回调中通过 `evbuffer_add()` 填充输出缓冲区，使用 `evhttp_send_reply()` 返回响应。
6. 信号回调调用 `event_base_loopbreak()` 退出循环，局部 `unique_ptr` 依次释放信号事件、HTTP 服务和事件循环。

请求路径由 `evhttp_request_get_evhttp_uri()` 和 `evhttp_uri_get_path()` 获取，查询参数不会成为路径的一部分。请求的输入、输出缓冲区由 libevent 管理；示例中只释放自己创建的服务和事件资源。

参考：[libevent 官方 HTTP 服务说明](https://libevent.org/libevent-book/Ref10_http_server.html)。

