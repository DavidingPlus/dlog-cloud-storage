# libevent 中的 HTTP 请求与 URI

`evhttp_request` 保存整个 HTTP 请求及其处理状态；`evhttp_uri` 保存这个请求中资源地址的解析结果。请求对象内部持有已解析的 URI 对象。

下面以 `Service::GenericCallback(evhttp_request *req, void *arg)` 收到的这条请求为例：

```http
GET /download/a%20b.txt?token=123 HTTP/1.1
Host: 127.0.0.1:8081

```

这里请求方法是 GET，没有请求正文。以下表格按数据含义展示对象中的内容，不是实际的结构体定义或字段名。

## evhttp_request 保存什么

回调参数 `req` 指向 libevent 已经解析好的当前 HTTP 请求。与这个例子有关的信息如下：

| 内容 | 值 |
| --- | --- |
| 请求方法 | `GET` |
| HTTP 版本 | `1.1` |
| 原始 URI 字符串 | `/download/a%20b.txt?token=123` |
| 请求头 | `Host` → `127.0.0.1:8081` |
| 请求正文 | 空 |
| 已解析的 URI | 指向下一节所描述的 `evhttp_uri` 对象 |

`req` 表示整个请求，因此后续上传、下载处理还可以通过它读取请求头和正文。回调参数 `arg` 则来自 `run()` 注册回调时传入的 `this`，用于取回当前 `Service` 对象，与 URI 中的查询参数无关。

## evhttp_uri 保存什么

libevent 用来构造 URI 对象的是请求行中的资源地址这一段：

```text
/download/a%20b.txt?token=123
```

解析后，通过 URI 读取接口可以得到以下值：

| URI 中的部分 | 该请求中的值 |
| --- | --- |
| 协议 `scheme` | 未设置，`nullptr` |
| 用户信息 `userinfo` | 未设置，`nullptr` |
| 主机 `host` | 未设置，`nullptr` |
| 端口 `port` | 未设置，`-1` |
| 路径 `path` | `/download/a%20b.txt` |
| 查询字符串 `query` | `token=123` |
| 片段 `fragment` | 未设置，`nullptr` |

这条资源地址只有路径和查询字符串，因此主机和端口未设置。`Host: 127.0.0.1:8081` 属于请求头，保存在 `evhttp_request` 的请求头信息中，不会复制进这个 URI 对象。请求方法 GET 和 HTTP 版本也属于请求本身的信息。

## 从 req 读取 URI、path 和 query

libevent 在解析 HTTP 请求时已经构建好 URI 对象。`evhttp_request_get_evhttp_uri()` 返回这个现有对象的指针：

```cpp
// req 指向整个请求；uri 指向请求内部已解析好的资源地址对象。
const evhttp_uri *uri = evhttp_request_get_evhttp_uri(req);

// 读取路径，结果为 "/download/a%20b.txt"。
const char *rawPath = evhttp_uri_get_path(uri);

// 读取查询字符串，结果为 "token=123"，不包含开头的 '?'。
const char *query = evhttp_uri_get_query(uri);
```

`evhttp_uri_get_path()` 负责提取路径，路径中的百分号编码仍然保留。项目中的 `utils::urlDecode()` 再把编码还原为原始字节：

```cpp
std::string requestPath = utils::urlDecode(rawPath);
// requestPath 为 "/download/a b.txt"。
```

`query` 此时仍是一整段查询字符串；需要读取其中的 `token` 等参数时，再单独解析键值。

这些读取接口返回请求内部数据的指针，不转移所有权。URI 对象和其中的字符串由 libevent 管理，在请求有效期内使用；复制到 `std::string` 后，字符串副本拥有自己的数据。

## 两个 request URI 接口的区别

两个接口都可以直接接收 `req`，分别读取地址的不同表示：

| 接口 | 返回类型 | 本例中的结果 |
| --- | --- | --- |
| `evhttp_request_get_uri(req)` | `const char *` | 原始字符串 `/download/a%20b.txt?token=123` |
| `evhttp_request_get_evhttp_uri(req)` | `const evhttp_uri *` | 已解析的 URI 对象，包含前表中的各部分 |

当前回调使用第二个接口，再读取其中的路径即可，无须先调用 `evhttp_request_get_uri()`。

## 当前 GenericCallback 为什么这样读取

当前实现分步读取 URI 和路径，先检查指针，再解码路径：

```cpp
const auto *uri = evhttp_request_get_evhttp_uri(req);
const char *rawPath = uri ? evhttp_uri_get_path(uri) : nullptr;
if (!rawPath)
{
    evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid request URI", nullptr);
    return;
}

std::string requestPath;
try
{
    requestPath = utils::urlDecode(rawPath);
}
catch (const std::invalid_argument &)
{
    evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid URL encoding", nullptr);
    return;
}
```

对于上述请求，三个步骤依次得到：

1. 请求内部已解析好的 `evhttp_uri` 对象。
2. 路径 `/download/a%20b.txt`。
3. 解码后的路径 `/download/a b.txt`。

回调按路径判断下载、上传或首页处理。查询参数 `token=123` 不参与当前路由匹配，完整的 `req` 和上下文 `arg` 会继续传给对应业务处理函数。

上述步骤和业务分发均位于通用回调最外层的 `try` 内。路径解码的参数错误返回 400；模板读取等未处理的业务异常由外层 `catch (...)` 统一返回 500，不把内部异常消息发送给客户端。`GenericCallback()` 声明为 `noexcept`，异常不会传播出 libevent 事件循环，失败请求之后仍可处理后续请求。

代码命名约定：`requestPath` 表示解码后的 HTTP 请求路径，用于路由及 `getFileMetaDataByRequestPath()` 查询；`filePath` 表示元数据记录的磁盘文件路径，用于文件读写及 `getFileMetaDataByFilePath()` 查询。
