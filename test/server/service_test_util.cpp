#include "service_test_util.h"


namespace service_test
{
    std::string Access::formatFileTime(fs::file_time_type time) { return server::Service::FormatFileTime(time); }

    std::string Access::formatFileSize(uint64_t size) { return server::Service::FormatFileSize(size); }

    std::string Access::fileListHtml(const std::vector<server::FileMetadata> &files) { return server::Service::GenerateFileListHtml(files); }

    std::string Access::etag(const server::FileMetadata &info) { return server::Service::GetETag(info); }

    Access::Callback Access::callback() { return &server::Service::GenericCallback; }

    const std::string &Access::ip(const server::Service &service) { return service.m_serverIp; }

    int Access::port(const server::Service &service) { return service.m_serverPort; }

    const std::string &Access::prefix(const server::Service &service) { return service.m_downloadPrefix; }

    server::DataManager &Access::data(server::Service &service) { return service.m_dataManager; }

    ScopedTimezone::ScopedTimezone(const char *zone)
    {
        if (const char *original = std::getenv("TZ")) m_original = original;
        if (0 != ::setenv("TZ", zone, 1)) throw std::runtime_error("setenv(TZ) failed");
        ::tzset();
    }

    ScopedTimezone::~ScopedTimezone()
    {
        if (m_original)
            ::setenv("TZ", m_original->data(), 1);
        else
            ::unsetenv("TZ");
        ::tzset();
    }

    fs::file_time_type FileTimeAt(int64_t unixSeconds, std::chrono::milliseconds fraction)
    {
        const auto fileNow = fs::file_time_type::clock::now();
        const auto systemNow = std::chrono::system_clock::now();
        const auto target = std::chrono::system_clock::time_point(std::chrono::seconds(unixSeconds) + fraction);
        return fileNow + std::chrono::duration_cast<fs::file_time_type::duration>(target - systemNow);
    }

    server::FileMetadata ETagInfo(const std::string &name, size_t size, fs::file_time_type::rep ticks)
    {
        server::FileMetadata info;
        info.m_absoluteFilePath = fs::path("/storage") / name;
        info.m_fileSize = size;
        info.m_mtime = fs::file_time_type(fs::file_time_type::duration(ticks));
        info.m_url = "/download/" + name;
        return info;
    }

    void ServiceFixture::SetUp()
    {
        m_originalWorkingDirectory = fs::current_path();
        m_configFile = utils::executableDirectory() / "config/server.conf";
        m_templateFile = utils::executableDirectory() / "web/template.html";
        m_originalConfig = utils::FileUtil(m_configFile).getContent();
        m_originalTemplate = utils::FileUtil(m_templateFile).getContent();
        m_templatePermissions = fs::status(m_templateFile).permissions();
        m_savedResources = true;

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const auto candidate = fs::temp_directory_path() / ("dlog-service-tests-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error))
            {
                m_directory = candidate;
                break;
            }
            ASSERT_FALSE(error) << error.message();
        }
        ASSERT_FALSE(m_directory.empty());
        m_metadataFile = m_directory / "metadata.json";
        writeMetadata({});
        m_config = utils::JsonUtil::UnSerialize(m_originalConfig);
        m_config["server_ip"] = "127.0.0.2";
        m_config["server_port"] = 18081;
        m_config["download_prefix"] = "/download/";
        m_config["compressed_files_dir"] = (m_directory / "compressed").string();
        m_config["uncompressed_files_dir"] = (m_directory / "uncompressed").string();
        m_config["metadata_file"] = m_metadataFile.string();
        applyConfig();
        writeTemplate("<!DOCTYPE html>\n<html><body>{{FILE_LIST}}</body></html>\n");
    }

    void ServiceFixture::TearDown()
    {
        std::error_code error;
        if (!m_originalWorkingDirectory.empty())
        {
            fs::current_path(m_originalWorkingDirectory, error);
            EXPECT_FALSE(error) << error.message();
        }
        if (m_savedResources)
        {
            // 删除模板、权限错误或目录占位等用例结束后，恢复原文件及其权限。
            fs::permissions(m_templateFile, fs::perms::owner_all, fs::perm_options::add, error);
            error.clear();
            fs::remove_all(m_templateFile, error);
            EXPECT_FALSE(error) << error.message();
            EXPECT_NO_THROW(utils::FileUtil(m_templateFile).setContent(m_originalTemplate));
            fs::permissions(m_templateFile, m_templatePermissions, error);
            EXPECT_FALSE(error) << error.message();
            EXPECT_NO_THROW(utils::FileUtil(m_configFile).setContent(m_originalConfig));
            EXPECT_NO_THROW(server::Config::GetInstance().readConfig());
        }
        if (!m_directory.empty())
        {
            fs::remove_all(m_directory, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    void ServiceFixture::applyConfig() const
    {
        utils::FileUtil(m_configFile).setContent(utils::JsonUtil::Serialize(m_config));
        server::Config::GetInstance().readConfig();
    }

    void ServiceFixture::writeTemplate(const std::string &content) const { utils::FileUtil(m_templateFile).setContent(content); }

    void ServiceFixture::writeMetadata(const std::vector<server::FileMetadata> &files) const
    {
        Json::Value records(Json::arrayValue);
        for (const auto &file : files)
        {
            Json::Value record;
            record["mtime"] = static_cast<Json::Int64>(file.m_mtime.time_since_epoch().count());
            record["file_size"] = static_cast<Json::UInt64>(file.m_fileSize);
            record["absolute_file_path"] = file.m_absoluteFilePath.string();
            record["url"] = file.m_url;
            records.append(record);
        }
        utils::FileUtil(m_metadataFile).setContent(utils::JsonUtil::Serialize(records));
    }

    server::FileMetadata ServiceFixture::makeInfo(const std::string &name, size_t size, bool compressed) const
    {
        server::FileMetadata file;
        file.m_mtime = FileTimeAt(946684800); // 2000-01-01 00:00:00 UTC。
        file.m_fileSize = size;
        file.m_absoluteFilePath = (compressed ? server::Config::GetInstance().getCompressedFilesDirPath() : server::Config::GetInstance().getUncompressedFilesDirPath()) / name;
        file.m_url = "/download/" + name;
        return file;
    }

    size_t ServiceFixture::Count(const std::string &text, const std::string &needle)
    {
        size_t count = 0;
        for (size_t pos = 0; std::string::npos != (pos = text.find(needle, pos)); pos += needle.size()) ++count;
        return count;
    }

    LocalHttpServer::LocalHttpServer(server::Service &service)
        : m_base(event_base_new(), event_base_free), m_http(m_base ? evhttp_new(m_base.get()) : nullptr, evhttp_free)
    {
        if (!m_http) throw std::runtime_error("cannot create test HTTP server");
        evhttp_set_gencb(m_http.get(), Access::callback(), &service);
        auto *listener = evhttp_bind_socket_with_handle(m_http.get(), "127.0.0.1", 0);
        if (!listener) throw std::runtime_error("cannot listen on an ephemeral loopback port");
        sockaddr_in address{};
        socklen_t length = sizeof(address);
        if (0 != ::getsockname(evhttp_bound_socket_get_fd(listener), reinterpret_cast<sockaddr *>(&address), &length))
            throw std::runtime_error("cannot read the test server port");
        m_port = ntohs(address.sin_port);
    }

    uint16_t LocalHttpServer::port() const { return m_port; }

    void LocalHttpServer::OnResponse(evhttp_request *req, void *arg)
    {
        auto &result = *static_cast<Result *>(arg);
        result.m_completed = true;
        if (req)
        {
            result.m_response.m_code = evhttp_request_get_response_code(req);
            const char *contentType = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Type");
            if (contentType) result.m_response.m_contentType = contentType;
            const char *contentLength = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Length");
            if (contentLength) result.m_response.m_contentLength = contentLength;
            const char *disposition = evhttp_find_header(evhttp_request_get_input_headers(req), "Content-Disposition");
            if (disposition) result.m_response.m_contentDisposition = disposition;
            const char *etag = evhttp_find_header(evhttp_request_get_input_headers(req), "ETag");
            if (etag) result.m_response.m_etag = etag;
            const char *acceptRanges = evhttp_find_header(evhttp_request_get_input_headers(req), "Accept-Ranges");
            if (acceptRanges) result.m_response.m_acceptRanges = acceptRanges;
            auto *body = evhttp_request_get_input_buffer(req);
            result.m_response.m_body.resize(evbuffer_get_length(body));
            if (!result.m_response.m_body.empty())
                evbuffer_copyout(body, result.m_response.m_body.data(), result.m_response.m_body.size());
        }
        event_base_loopbreak(result.m_base);
    }

    void LocalHttpServer::OnTimeout(evutil_socket_t, short, void *arg) { event_base_loopbreak(static_cast<event_base *>(arg)); }

    Response LocalHttpServer::get(const std::string &uri, const std::vector<std::pair<std::string, std::string>> &headers)
    {
        return request(EVHTTP_REQ_GET, uri, headers, "");
    }

    Response LocalHttpServer::post(const std::string &uri, const std::vector<std::pair<std::string, std::string>> &headers, const std::string &body)
    {
        return request(EVHTTP_REQ_POST, uri, headers, body);
    }

    Response LocalHttpServer::request(evhttp_cmd_type method, const std::string &uri, const std::vector<std::pair<std::string, std::string>> &headers, const std::string &body)
    {
        Result result{m_base.get(), {}, false};
        std::unique_ptr<evhttp_connection, decltype(&evhttp_connection_free)> connection(
            evhttp_connection_base_new(m_base.get(), nullptr, "127.0.0.1", m_port), evhttp_connection_free);
        if (!connection) throw std::runtime_error("cannot create test HTTP client");
        evhttp_connection_set_timeout(connection.get(), 2);
        RequestPtr request(evhttp_request_new(OnResponse, &result), evhttp_request_free);
        if (!request) throw std::runtime_error("cannot allocate test HTTP request");
        evhttp_add_header(evhttp_request_get_output_headers(request.get()), "Host", "127.0.0.1");
        evhttp_add_header(evhttp_request_get_output_headers(request.get()), "Connection", "close");
        for (const auto &header : headers)
        {
            // 显式请求头覆盖默认值，便于测试不同 Host 和代理访问。
            evhttp_remove_header(evhttp_request_get_output_headers(request.get()), header.first.data());
            evhttp_add_header(evhttp_request_get_output_headers(request.get()), header.first.data(), header.second.data());
        }
        evbuffer_add(evhttp_request_get_output_buffer(request.get()), body.data(), body.size());
        // make_request() 接管 request；成功与失败都不再由调用方释放。
        if (0 != evhttp_make_request(connection.get(), request.release(), method, uri.data()))
            throw std::runtime_error("cannot schedule test HTTP request");

        std::unique_ptr<event, decltype(&event_free)> timeout(evtimer_new(m_base.get(), OnTimeout, m_base.get()), event_free);
        const timeval limit{3, 0};
        if (!timeout || 0 != evtimer_add(timeout.get(), &limit)) throw std::runtime_error("cannot set request timeout");
        if (-1 == event_base_dispatch(m_base.get()) || !result.m_completed || 0 == result.m_response.m_code)
            throw std::runtime_error("test HTTP request failed or timed out");
        return result.m_response;
    }
}
