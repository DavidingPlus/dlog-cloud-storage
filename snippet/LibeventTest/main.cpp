#include <iostream>
#include <csignal>
#include <memory>
#include <string>
#include <string_view>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>


namespace
{
    const std::string kAddress = "127.0.0.1";

    constexpr int kPort = 8082;


    // HTTP 请求回调：libevent 已完成请求解析，可以直接读取 URI、请求头和正文。
    void genHandler(evhttp_request *req, void *)
    {
        auto *uri = evhttp_request_get_evhttp_uri(req);
        if (!uri)
        {
            evhttp_send_error(req, HTTP_BADREQUEST, nullptr);
            return;
        }

        auto *rawPath = evhttp_uri_get_path(uri);
        std::string_view path = rawPath && *rawPath ? rawPath : "/";

        std::cout << "Request path: " << path << std::endl;

        if ("/" != path)
        {
            evhttp_send_error(req, HTTP_NOTFOUND, nullptr);
            return;
        }

        if (EVHTTP_REQ_GET != evhttp_request_get_command(req))
        {
            evhttp_add_header(evhttp_request_get_output_headers(req), "Allow", "GET");
            evhttp_send_error(req, HTTP_BADMETHOD, nullptr);
            return;
        }

        // 请求的输出缓冲区由 libevent 管理，无需手动释放。
        auto *output = evhttp_request_get_output_buffer(req);
        auto *headers = evhttp_request_get_output_headers(req);
        constexpr std::string_view body = "Hello, libevent!\n";

        if (0 != evhttp_add_header(headers, "Content-Type", "text/plain; charset=utf-8") || 0 != evbuffer_add(output, body.data(), body.size()))
        {
            evhttp_send_error(req, HTTP_INTERNAL, nullptr);
            return;
        }

        // 正文已放入请求的输出缓冲区，因此最后一个参数传 nullptr。
        evhttp_send_reply(req, HTTP_OK, "OK", nullptr);
    }

    // evsignal_new() 将信号交给事件循环处理，arg 是注册回调时传入的指针。
    void signalHandler(evutil_socket_t, short, void *arg)
    {
        std::cout << "\nSIGINT received, leaving the event loop." << std::endl;

        event_base_loopbreak(static_cast<event_base *>(arg));
    }
}


int main()
{
    // 1. 创建事件循环。unique_ptr 负责在退出时调用对应的 libevent 释放函数。
    std::unique_ptr<event_base, decltype(&event_base_free)> base(event_base_new(), event_base_free);
    if (!base)
    {
        std::cerr << "event_base_new() failed\n";
        return EXIT_FAILURE;
    }

    // 2. 在同一个事件循环上创建 HTTP 服务。
    std::unique_ptr<evhttp, decltype(&evhttp_free)> http(evhttp_new(base.get()), evhttp_free);
    if (!http)
    {
        std::cerr << "evhttp_new() failed\n";
        return EXIT_FAILURE;
    }

    // 3. 绑定监听地址和端口；使用 8082，与项目服务端默认的 8081 区分。
    if (0 != evhttp_bind_socket(http.get(), kAddress.data(), static_cast<ev_uint16_t>(kPort)))
    {
        std::cerr << "evhttp_bind_socket() failed: " << kAddress << ':' << kPort << '\n';
        return EXIT_FAILURE;
    }

    // 4. 注册通用请求回调，在回调中检查请求路径和 HTTP 方法。
    evhttp_set_gencb(http.get(), genHandler, nullptr);

    // 5. 注册 Ctrl+C 对应的 SIGINT 信号事件，回调参数传入事件循环指针。
    std::unique_ptr<event, decltype(&event_free)> signalEvent(evsignal_new(base.get(), SIGINT, signalHandler, base.get()), event_free);
    if (!signalEvent || 0 != event_add(signalEvent.get(), nullptr))
    {
        std::cerr << "register SIGINT event failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "libevent " << event_get_version() << ", backend: " << event_base_get_method(base.get()) << '\n'
              << "Listening on http://" << kAddress << ':' << kPort << "/ (Ctrl+C to stop)"
              << std::endl;

    // 6. 阻塞等待事件并调用回调，收到退出请求后返回。
    if (-1 == event_base_dispatch(base.get()))
    {
        std::cerr << "event_base_dispatch() failed\n";
        return EXIT_FAILURE;
    }

    // 局部变量按构造的反序析构：信号事件 -> HTTP 服务 -> 事件循环。
    std::cout << "Event loop stopped; releasing resources." << std::endl;
}
