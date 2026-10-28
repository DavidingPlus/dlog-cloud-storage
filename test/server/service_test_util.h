#ifndef _DLOG_CLOUD_STORAGE_SERVICE_TEST_UTIL_H_
#define _DLOG_CLOUD_STORAGE_SERVICE_TEST_UTIL_H_

#include <gtest/gtest.h>

#include "server/service.h"
#include "utils/common.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>


namespace service_test
{
    namespace fs = std::filesystem;

    using RequestPtr = std::unique_ptr<evhttp_request, decltype(&evhttp_request_free)>;

    // 只在测试目标启用 -fno-access-control，集中访问私有接口，不修改 Service 的类定义。
    class Access
    {
    public:

        using Callback = void (*)(evhttp_request *, void *);

        static std::string formatFileTime(fs::file_time_type time);
        static std::string formatFileSize(uint64_t size);
        static std::string fileListHtml(const std::vector<server::FileMetadata> &files);
        static std::string etag(const server::FileMetadata &info);
        static void dispatch(evhttp_request *req, server::Service &service);
        static void showFileList(evhttp_request *req, server::Service &service);
        static Callback callback();
        static const std::string &ip(const server::Service &service);
        static int port(const server::Service &service);
        static const std::string &prefix(const server::Service &service);
        static server::DataManager &data(server::Service &service);
    };

    RequestPtr MakeRequest(const std::string &uri = "/");

    // 修改进程时区后自动恢复；时区用例顺序执行，并发格式化期间不再修改 TZ。
    class ScopedTimezone
    {
    public:

        explicit ScopedTimezone(const char *zone);
        ~ScopedTimezone();
        ScopedTimezone(const ScopedTimezone &) = delete;
        ScopedTimezone &operator=(const ScopedTimezone &) = delete;

    private:

        std::optional<std::string> m_original;
    };

    // 保留半秒余量，避免 C++17 近似换算两种时钟时恰好跨过整秒边界。
    fs::file_time_type FileTimeAt(int64_t unixSeconds, std::chrono::milliseconds fraction = std::chrono::milliseconds(500));
    server::FileMetadata ETagInfo(const std::string &name = "a.txt", size_t size = 1024, fs::file_time_type::rep ticks = 123456);

    // 配置、模板和元数据均隔离；包括异常用例在内，每次测试后恢复构建目录中的原资源。
    class ServiceFixture : public ::testing::Test
    {
    protected:

        void SetUp() override;
        void TearDown() override;
        void applyConfig() const;
        void writeTemplate(const std::string &content) const;
        void writeMetadata(const std::vector<server::FileMetadata> &files) const;
        server::FileMetadata makeInfo(const std::string &name = "report.txt", size_t size = 1536, bool compressed = false) const;
        static size_t Count(const std::string &text, const std::string &needle);

        fs::path m_configFile;
        fs::path m_templateFile;
        fs::path m_metadataFile;
        fs::path m_directory;
        fs::path m_originalWorkingDirectory;
        std::string m_originalConfig;
        std::string m_originalTemplate;
        Json::Value m_config;

    private:

        fs::perms m_templatePermissions = fs::perms::unknown;
        bool m_savedResources = false;
    };

    struct SizeCase
    {
        uint64_t m_bytes;
        const char *m_expected;
    };

    class ServiceSizeTests : public ::testing::TestWithParam<SizeCase>
    {
    };
    class ServiceConstructorTests : public ServiceFixture
    {
    };
    class ServiceHtmlTests : public ServiceFixture
    {
    };
    class ServiceETagTests : public ::testing::Test
    {
    };
    class ServiceTimeTests : public ::testing::Test
    {
    protected:

        ScopedTimezone m_timezone{"UTC0"};
    };
    class ServiceHttpTests : public ServiceFixture
    {
    };
    class ServiceRoutingTests : public ServiceFixture
    {
    };
    class ServiceRunTests : public ServiceFixture
    {
    };

    struct Response
    {
        int m_code = 0;
        std::string m_contentType;
        std::string m_body;
    };

    // 同一个真实事件循环驱动 HTTP 服务与客户端，使用系统分配的本地端口和有上限的等待。
    class LocalHttpServer
    {
    public:

        explicit LocalHttpServer(server::Service &service);
        uint16_t port() const;
        Response get(const std::string &uri);
        Response post(const std::string &uri, const std::vector<std::pair<std::string, std::string>> &headers, const std::string &body);

    private:

        Response request(evhttp_cmd_type method, const std::string &uri, const std::vector<std::pair<std::string, std::string>> &headers, const std::string &body);

        struct Result
        {
            event_base *m_base;
            Response m_response;
            bool m_completed = false;
        };

        static void OnResponse(evhttp_request *req, void *arg);
        static void OnTimeout(evutil_socket_t, short, void *arg);

        std::unique_ptr<event_base, decltype(&event_base_free)> m_base;
        std::unique_ptr<evhttp, decltype(&evhttp_free)> m_http;
        uint16_t m_port = 0;
    };
}


#endif
