#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#include <bundle.h>

#include "server/config.h"
#include "utils/common.h"
#include "utils/fileutil.h"
#include "utils/jsonutil.h"


namespace
{
    namespace fs = std::filesystem;

    // 读取 XMake 复制的初始配置，不访问单例，供首次初始化测试复用。
    class ConfigFileTests : public ::testing::Test
    {

    protected:

        void SetUp() override
        {
            m_originalWorkingDirectory = fs::current_path();
            m_configFile = utils::executableDirectory() / "config" / "server.conf";
            m_originalContent = utils::FileUtil(m_configFile).getContent();
            m_configPermissions = fs::status(m_configFile).permissions();
        }

        void TearDown() override
        {
            std::error_code error;
            if (!m_originalWorkingDirectory.empty())
            {
                fs::current_path(m_originalWorkingDirectory, error);
                EXPECT_FALSE(error) << error.message();
            }
            if (m_configPermissions == fs::perms::unknown)
            {
                return;
            }

            // 权限错误、目录占位等用例即使提前结束，也能恢复测试配置。
            if (fs::exists(m_configFile))
            {
                fs::permissions(m_configFile, m_configPermissions, error);
                EXPECT_FALSE(error) << error.message();
                if (fs::is_directory(m_configFile))
                {
                    fs::remove(m_configFile, error);
                    EXPECT_FALSE(error) << error.message();
                }
            }

            // 测试配置位于测试程序旁边，与服务端 bin/config 下的配置相互独立。
            utils::FileUtil(m_configFile).setContent(m_originalContent);

            if (!m_workingDirectory.empty())
            {
                fs::remove_all(m_workingDirectory, error);
                EXPECT_FALSE(error) << error.message();
            }
        }

        Json::Value validConfig() const
        {
            return utils::JsonUtil::UnSerialize(m_originalContent);
        }

        Json::Value updatedConfig() const
        {
            auto root = validConfig();
            root["server_ip"] = "0.0.0.0";
            root["server_port"] = 9090;
            root["download_prefix"] = "/files/";
            root["deep_storage_dir"] = "./data/compressed/";
            root["low_storage_dir"] = "./data/normal/";
            root["storage_info"] = "./data/metadata.json";
            root["bundle_format"] = bundle::RAW;
            return root;
        }

        // 当前目录放一份不同的配置，确认读取位置由可执行文件决定。
        void prepareWorkingDirectory()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                const auto candidate = fs::temp_directory_path() / ("dlog-config-tests-" + std::to_string(stamp) + "-" + std::to_string(attempt));
                std::error_code error;
                if (fs::create_directory(candidate, error))
                {
                    m_workingDirectory = candidate;
                    fs::create_directory(candidate / "config");
                    utils::FileUtil(candidate / "config" / "server.conf").setContent(Json::writeString(Json::StreamWriterBuilder{}, updatedConfig()));
                    fs::current_path(candidate);
                    return;
                }
                ASSERT_FALSE(error) << error.message();
            }
            FAIL() << "Cannot create an isolated working directory";
        }

        void writeConfig(const Json::Value &root) const
        {
            utils::FileUtil(m_configFile).setContent(Json::writeString(Json::StreamWriterBuilder{}, root));
        }

        static void ExpectConfig(const Json::Value &root)
        {
            const auto &config = server::Config::GetInstance();
            EXPECT_EQ(config.getServerIp(), root["server_ip"].asString());
            EXPECT_EQ(config.getServerPort(), root["server_port"].asInt());
            EXPECT_EQ(config.getDownloadPrefix(), root["download_prefix"].asString());
            EXPECT_EQ(config.getDeepStorageDir(), root["deep_storage_dir"].asString());
            EXPECT_EQ(config.getLowStorageDir(), root["low_storage_dir"].asString());
            EXPECT_EQ(config.getStorageInfoFile(), root["storage_info"].asString());
            EXPECT_EQ(config.getBundleFormat(), root["bundle_format"].asInt());
        }

        fs::path m_configFile;
        fs::path m_originalWorkingDirectory;
        fs::path m_workingDirectory;
        std::string m_originalContent;
        fs::perms m_configPermissions = fs::perms::unknown;
    };

    class ConfigTests : public ConfigFileTests
    {

    protected:

        void SetUp() override
        {
            ConfigFileTests::SetUp();
            ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
        }
    };

    class ConfigInitializationDeathTest : public ConfigFileTests
    {

    protected:

        void SetUp() override
        {
            ConfigFileTests::SetUp();
            // 重新启动测试程序，避免继承其他测试已经构造好的单例。
            GTEST_FLAG_SET(death_test_style, "threadsafe");
        }
    };
}


// 首次构造在新进程中执行；子进程的断言失败必须反映到退出码。

TEST_F(ConfigInitializationDeathTest, FirstAccessLoadsAllFieldsBesideExecutable)
{
    ASSERT_EXIT(
        {
            fs::current_path(fs::temp_directory_path());
            // 只调用 GetInstance()，确认构造函数完成加载。
            ExpectConfig(validConfig());
            std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(ConfigInitializationDeathTest, RetriesInitializationAfterMissingFile)
{
    ASSERT_EXIT(
        {
            fs::remove(m_configFile);
            EXPECT_THROW(server::Config::GetInstance(), fs::filesystem_error);

            writeConfig(updatedConfig());
            ExpectConfig(updatedConfig());
            std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(ConfigInitializationDeathTest, RetriesInitializationAfterMalformedJson)
{
    ASSERT_EXIT(
        {
            utils::FileUtil(m_configFile).setContent("{");
            EXPECT_THROW(server::Config::GetInstance(), std::invalid_argument);

            writeConfig(updatedConfig());
            ExpectConfig(updatedConfig());
            std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST_F(ConfigInitializationDeathTest, RetriesInitializationAfterRepeatedValidationFailures)
{
    ASSERT_EXIT(
        {
            for (const char *field : {"server_ip", "server_port", "download_prefix", "deep_storage_dir", "low_storage_dir", "storage_info", "bundle_format"})
            {
                SCOPED_TRACE(field);
                auto root = updatedConfig();
                root.removeMember(field);
                writeConfig(root);
                EXPECT_THROW(server::Config::GetInstance(), std::invalid_argument);
            }

            writeConfig(updatedConfig());
            ExpectConfig(updatedConfig());
            std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}


// 已加载配置的读取、校验和失败恢复。

TEST_F(ConfigTests, LoadsAllSevenFields)
{
    ExpectConfig(validConfig());
}

TEST_F(ConfigTests, FindsConfigurationBesideExecutableAfterWorkingDirectoryChanges)
{
    prepareWorkingDirectory();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());

    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    ExpectConfig(validConfig());
}

TEST_F(ConfigTests, ReloadUpdatesAllFields)
{
    auto root = updatedConfig();
    writeConfig(root);

    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    ExpectConfig(root);
}

TEST_F(ConfigTests, ReturnsTheSameInstanceWithoutImplicitReload)
{
    auto &first = server::Config::GetInstance();
    writeConfig(updatedConfig());

    auto &second = server::Config::GetInstance();
    EXPECT_EQ(&first, &second);
    ExpectConfig(validConfig());

    ASSERT_NO_THROW(second.readConfig());
    EXPECT_EQ(&first, &server::Config::GetInstance());
    ExpectConfig(updatedConfig());
}

TEST_F(ConfigTests, RejectsMalformedOrNonObjectDocuments)
{
    for (const char *content : {"", " \t\r\n", "{", "{\"server_ip\":}", "[", "[]", "[1, 2]", "null", "true", "false", "42", "1.5", "\"config\"", "{}"})
    {
        SCOPED_TRACE(content);
        utils::FileUtil(m_configFile).setContent(content);
        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        ExpectConfig(validConfig());
    }
}

TEST_F(ConfigTests, RejectsMissingFieldsBeforeUpdatingMembers)
{
    for (const char *field : {"server_ip", "server_port", "download_prefix", "deep_storage_dir", "low_storage_dir", "storage_info", "bundle_format"})
    {
        SCOPED_TRACE(field);
        auto root = updatedConfig();
        root.removeMember(field);
        writeConfig(root);

        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        ExpectConfig(validConfig());
    }
}

TEST_F(ConfigTests, RejectsEmptyOrNonStringFields)
{
    for (const char *field : {"server_ip", "download_prefix", "deep_storage_dir", "low_storage_dir", "storage_info"})
    {
        SCOPED_TRACE(field);
        for (const auto &value : {Json::Value(), Json::Value(""), Json::Value(42), Json::Value(1.5), Json::Value(false), Json::Value(true), Json::Value(Json::arrayValue), Json::Value(Json::objectValue)})
        {
            SCOPED_TRACE(Json::writeString(Json::StreamWriterBuilder{}, value));
            auto root = updatedConfig();
            root[field] = value;
            writeConfig(root);

            EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
            ExpectConfig(validConfig());
        }
    }
}

TEST_F(ConfigTests, RejectsNonIntegerPortsAndCompressionFormats)
{
    for (const char *field : {"server_port", "bundle_format"})
    {
        SCOPED_TRACE(field);
        for (const auto &value : {Json::Value(), Json::Value(""), Json::Value("8081"), Json::Value(false), Json::Value(true), Json::Value(-0.5), Json::Value(0.5), Json::Value(1.5), Json::Value(Json::arrayValue), Json::Value(Json::objectValue)})
        {
            SCOPED_TRACE(Json::writeString(Json::StreamWriterBuilder{}, value));
            auto root = updatedConfig();
            root[field] = value;
            writeConfig(root);

            EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
            ExpectConfig(validConfig());
        }
    }
}

TEST_F(ConfigTests, RejectsPortsOutsideTheAllowedRange)
{
    for (int port : {std::numeric_limits<int>::min(), -1, 0, 65536, std::numeric_limits<int>::max()})
    {
        SCOPED_TRACE(port);
        auto root = updatedConfig();
        root["server_port"] = port;
        writeConfig(root);

        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        ExpectConfig(validConfig());
    }
}

TEST_F(ConfigTests, RejectsUnsupportedCompressionFormats)
{
    for (int format : {std::numeric_limits<int>::min(), -1, bundle::BZIP2 + 1, std::numeric_limits<int>::max()})
    {
        SCOPED_TRACE(format);
        auto root = updatedConfig();
        root["bundle_format"] = format;
        writeConfig(root);

        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        ExpectConfig(validConfig());
    }
}

TEST_F(ConfigTests, RejectsNumbersOutsideIntRangeWithoutTruncation)
{
    const Json::Value values[] = {
        Json::Value(static_cast<Json::Int64>(std::numeric_limits<int>::min()) - 1),
        Json::Value(static_cast<Json::Int64>(std::numeric_limits<int>::max()) + 1),
        Json::Value(std::numeric_limits<Json::Int64>::min()),
        Json::Value(std::numeric_limits<Json::Int64>::max()),
        Json::Value(std::numeric_limits<Json::UInt>::max()),
        Json::Value(std::numeric_limits<Json::UInt64>::max()),
        Json::Value(-1e100),
        Json::Value(1e100)};

    for (const char *field : {"server_port", "bundle_format"})
    {
        SCOPED_TRACE(field);
        for (const auto &value : values)
        {
            SCOPED_TRACE(Json::writeString(Json::StreamWriterBuilder{}, value));
            auto root = updatedConfig();
            root[field] = value;
            writeConfig(root);

            EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
            ExpectConfig(validConfig());
        }
    }
}

TEST_F(ConfigTests, AcceptsPortBoundariesAndSupportedCompressionFormats)
{
    for (int port : {1, 65535})
    {
        SCOPED_TRACE(port);
        for (int format = bundle::RAW; format <= bundle::BZIP2; ++format)
        {
            SCOPED_TRACE(format);
            auto root = validConfig();
            root["server_port"] = port;
            root["bundle_format"] = format;
            writeConfig(root);

            ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
            ExpectConfig(root);
        }
    }
}

TEST_F(ConfigTests, AcceptsIntegralRealNumbersAccordingToJsonCppSemantics)
{
    // isInt() 判断数值能否表示为 int，因此 8081.0 这样的整数值也合法。
    auto root = updatedConfig();
    root["server_port"] = 8081.0;
    root["bundle_format"] = static_cast<double>(bundle::LZIP);
    writeConfig(root);

    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    ExpectConfig(root);
}

TEST_F(ConfigTests, RejectsDownloadPrefixesWithoutLeadingOrTrailingSlash)
{
    for (const char *prefix : {"download/", "/download", "download", " /download/", "/download/ ", "\\", "\\download\\", "/download\\", "\\download/"})
    {
        SCOPED_TRACE(prefix);
        auto root = updatedConfig();
        root["download_prefix"] = prefix;
        writeConfig(root);

        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        ExpectConfig(validConfig());
    }
}

TEST_F(ConfigTests, AcceptsRootAndNestedDownloadPrefixes)
{
    for (const char *prefix : {"/", "/download/", "/api/files/", u8"/下载 目录/"})
    {
        SCOPED_TRACE(prefix);
        auto root = updatedConfig();
        root["download_prefix"] = prefix;
        writeConfig(root);

        ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
        ExpectConfig(root);
    }
}

TEST_F(ConfigTests, PreservesUnicodeAndFilePathsAsConfigured)
{
    auto root = updatedConfig();
    root["server_ip"] = "::1";
    root["download_prefix"] = u8"/云存储/";
    root["deep_storage_dir"] = u8"/srv/cloud/深度 存储/";
    root["low_storage_dir"] = u8"../普通 存储/";
    root["storage_info"] = u8"./元数据/文件 \"信息\".json";
    writeConfig(root);

    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    ExpectConfig(root);
}

TEST_F(ConfigTests, IgnoresUnknownFields)
{
    auto root = updatedConfig();
    root["future_option"]["enabled"] = true;
    root["comment"] = "additional metadata";
    writeConfig(root);

    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    ExpectConfig(root);
}

TEST_F(ConfigTests, MissingFileReportsTheFileErrorAndKeepsLoadedConfiguration)
{
    fs::remove(m_configFile);

    EXPECT_THROW(server::Config::GetInstance().readConfig(), fs::filesystem_error);
    ExpectConfig(validConfig());
}

TEST_F(ConfigTests, DoesNotFallBackToConfigurationInWorkingDirectory)
{
    prepareWorkingDirectory();
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    fs::remove(m_configFile);

    EXPECT_THROW(server::Config::GetInstance().readConfig(), fs::filesystem_error);
    ExpectConfig(validConfig());
}

TEST_F(ConfigTests, DirectoryAtConfigFilePathReportsTheFileError)
{
    fs::remove(m_configFile);
    fs::create_directory(m_configFile);

    EXPECT_THROW(server::Config::GetInstance().readConfig(), fs::filesystem_error);
    ExpectConfig(validConfig());
}

TEST_F(ConfigTests, UnreadableFileReportsTheStreamError)
{
    fs::permissions(m_configFile, fs::perms::none);
    std::ifstream probe(m_configFile, std::ios::binary);
    if (probe.is_open())
    {
        GTEST_SKIP() << "The current user can read files without read permission";
    }

    EXPECT_THROW(server::Config::GetInstance().readConfig(), std::ios_base::failure);
    ExpectConfig(validConfig());
}

TEST_F(ConfigTests, CanReloadAfterTheMissingFileIsRestored)
{
    fs::remove(m_configFile);
    EXPECT_THROW(server::Config::GetInstance().readConfig(), fs::filesystem_error);
    ExpectConfig(validConfig());

    writeConfig(updatedConfig());
    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    ExpectConfig(updatedConfig());
}

TEST_F(ConfigTests, CanReloadAfterMalformedJsonIsFixed)
{
    utils::FileUtil(m_configFile).setContent("{");
    EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
    ExpectConfig(validConfig());

    writeConfig(updatedConfig());
    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    ExpectConfig(updatedConfig());
}

TEST_F(ConfigTests, CanReloadAfterValidationFailuresWithoutPartialUpdates)
{
    auto &config = server::Config::GetInstance();
    for (const char *field : {"server_ip", "server_port", "download_prefix", "deep_storage_dir", "low_storage_dir", "storage_info", "bundle_format"})
    {
        SCOPED_TRACE(field);
        auto invalid = updatedConfig();
        invalid[field] = Json::Value();
        writeConfig(invalid);
        EXPECT_THROW(config.readConfig(), std::invalid_argument);
        ExpectConfig(validConfig());
    }

    writeConfig(updatedConfig());
    ASSERT_NO_THROW(config.readConfig());
    EXPECT_EQ(&config, &server::Config::GetInstance());
    ExpectConfig(updatedConfig());

    // 已经加载过新配置后再次失败，应该保留新值而非恢复为默认值。
    auto invalid = validConfig();
    invalid["download_prefix"] = "/invalid";
    writeConfig(invalid);
    EXPECT_THROW(config.readConfig(), std::invalid_argument);
    ExpectConfig(updatedConfig());
}
