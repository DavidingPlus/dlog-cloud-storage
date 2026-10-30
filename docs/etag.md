# ETag 基础知识

ETag（Entity Tag，实体标签）是服务端为资源的某个表示提供的版本标识。对本项目，可以把它理解为“当前下载内容的版本标签”。服务端通过 `ETag` 响应头发送标签，客户端保存后，在后续请求中原样带回；客户端无须解析标签内部的生成规则。[RFC 9110：ETag](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3)

ETag 用于版本比较。它是否由文件属性、版本号或内容摘要生成，由服务端决定；不能仅凭一个标签字符串推断其校验能力。

## 缓存验证：避免重复传输

第一次下载时，服务端返回文件内容和标签。以下报文只展示相关字段：

```http
HTTP/1.1 200 OK
ETag: "v1"

<文件内容>
```

客户端已有可复用的完整副本，需要验证版本时，可以发送：

```http
GET /download/a.txt HTTP/1.1
Host: localhost:8081
If-None-Match: "v1"
```

在这个 GET 请求中，标签匹配时返回 `304 Not Modified`，客户端复用已有正文；不匹配时返回新正文和新标签。`304` 本身不携带文件内容。[RFC 9110：If-None-Match](https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.2)、[304](https://www.rfc-editor.org/rfc/rfc9110.html#section-15.4.5)

ETag 参与验证版本，是否允许缓存、何时需要验证还取决于 `Cache-Control` 等缓存规则。浏览器可能直接复用仍然新鲜的缓存，并非每次访问都向服务端发送条件请求。[RFC 9111：缓存复用与验证](https://www.rfc-editor.org/rfc/rfc9111.html#section-4)

## 断点续传：确认已有片段属于同一版本

假设客户端已经保存文件前 1024 字节，并记住对应的强 ETag，可以请求后续字节：

```http
GET /download/a.txt HTTP/1.1
Host: localhost:8081
Range: bytes=1024-
If-Range: "v1"
```

支持范围请求的服务端在标签匹配、范围有效时返回 `206 Partial Content`，发送指定片段并设置 `Content-Range`；标签不匹配时忽略范围条件，返回完整新文件。`If-Range` 使用 ETag 时必须使用强标签；没有 `Range` 时应忽略 `If-Range`。[RFC 9110：If-Range](https://www.rfc-editor.org/rfc/rfc9110.html#section-13.1.5)

仅比较标签后返回 `206`，却仍发送完整文件，不构成正确的断点续传实现。

## 标签格式与强弱规则

| 类型 | 响应头示例 | 含义 |
| --- | --- | --- |
| 强 ETag | `ETag: "v1"` | 同一资源下，标签相同应保证对应内容逐字节一致 |
| 弱 ETag | `ETag: W/"v1"` | 可表示服务端认为可替代的版本，不保证字节完全一致 |

标签值需要双引号，弱标签增加大小写固定的 `W/` 前缀。不满足强校验条件时必须标为弱标签；弱标签也需要合理的版本等价规则，添加前缀并不能消除生成规则中的碰撞。[RFC 9110：强弱校验器](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.1)、[标签格式](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3)

缓存验证中的 `If-None-Match` 使用弱比较，可以接受弱标签；断点续传中的 `If-Range` 使用强比较，不能使用弱标签。标签属于不透明值，比较其内容时区分大小写。[RFC 9110：标签比较](https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3.2)

## 缓存清理与正文丢失

浏览器 HTTP 缓存副本和用户手动保存到“下载”目录的文件分别管理。HTTP 缓存条目包含响应头与正文，ETag 是响应头的一部分；正常清理该条目后，对应标签也随之删除或失效。[Chromium 缓存条目设计](https://www.chromium.org/developers/design-documents/network-stack/disk-cache/#external-interface)

自定义缓存可能出现“只剩 ETag、没有正文”的状态。例如网页代码把 ETag 另存到 `localStorage`，正文留在 HTTP 缓存里；用户只清缓存而未清站点数据，便可能留下单独保存的标签。Chrome 将缓存与站点数据作为不同类别管理。[Chrome 清理说明](https://support.google.com/chrome/answer/2392709?hl=en)

如果客户端在正文丢失后仍主动发送旧标签，版本未变时服务端仍会返回 `304`。服务端无法据此知道客户端已丢失正文，标签也不能还原文件。

自行实现客户端时应检查本地副本：

- 完整正文可用时，再发送对应的缓存验证条件。
- 正文缺失或损坏时，不依靠旧标签验证整份缓存，直接请求完整文件。
- 已收到 `304` 才发现正文不可用时，去掉 `If-None-Match`、`If-Modified-Since` 等缓存验证条件，重新发送 GET。
- 只保存部分正文且需要续传时，按 `Range` 与强校验器的规则处理。

这是客户端的恢复策略。具体浏览器如何处理异常缓存取决于实现，不能假定所有浏览器都会自动重试。

## 本项目中的职责与生成规则

`Service::GetETag(const FileMetadata &info)` 已实现标签生成，返回带双引号的 `"文件名-文件大小-修改时间"`。`Download()` 已将标签写入响应头，并返回完整原始文件内容；If-None-Match 等缓存条件和 Range/If-Range 判断属于可选增强，第一版不要求实现。

本项目确定沿用参考实现，以“文件名-文件大小-修改时间”生成自定义 ETag。在文件由本服务管理、内容更新会反映到文件大小或修改时间的约束下，按强 ETag 使用。生成时使用以下元数据：[参考 GetETag 实现](https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/src/server/Service.hpp)

| 信息 | 当前字段或接口 |
| --- | --- |
| 文件名 | `info.m_absoluteFilePath.filename()` |
| 存储字节数 | `info.m_fileSize` |
| 文件修改时间原始计数 | `info.m_mtime.time_since_epoch().count()` |

生成时需要注意：

- 同一版本应生成稳定标签，避免使用请求发生时的当前时间或随机数。
- 使用原始元数据，不使用面向展示的 `FormatFileSize()`、`FormatFileTime()`；展示结果会舍入或涉及时区转换。
- 文件名保留 ASCII 字母、数字以及 `-_.~`，其余字节按 `%HH` 编码后再拼接；例如 `a b.txt` 变为 `a%20b.txt`，文件名中原有的 `%` 也会编码，避免不同名称生成相同的编码结果。
- 本方案依赖受控的文件更新流程：内容改变时，文件大小或修改时间必须随之变化，并同步刷新元数据。直接修改内容后保留原有大小和修改时间，会破坏强校验约定。
- 本项目下载返回原始内容，压缩文件的 `m_fileSize` 则记录存储大小；更新存储文件后，需要同步刷新参与标签生成的元数据。

第一版仍按[项目计划的断点下载说明](plan.md#断点下载)返回完整文件，不支持外部进程修改数据文件或元数据。只有选择实现缓存或断点下载时，才增加相应请求处理；`GetETag()` 本身仅生成版本标识。
