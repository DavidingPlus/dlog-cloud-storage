#include <gtest/gtest.h>

#include <filesystem>
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

    class ConfigTests : public ::testing::Test
    {

    protected:

        void SetUp() override
        {
            m_originalWorkingDirectory = fs::current_path();
            m_configFile = utils::executableDirectory() / "config" / "server.conf";
            m_hadDirectory = fs::exists(m_configFile.parent_path());
            m_hadFile = fs::exists(m_configFile);
            if (m_hadFile)
            {
                m_originalContent = utils::FileUtil(m_configFile).getContent();
            }

            fs::create_directories(m_configFile.parent_path());
            writeConfig(validConfig());
            ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
        }

        void TearDown() override
        {
            std::error_code error;
            if (!m_originalWorkingDirectory.empty())
            {
                fs::current_path(m_originalWorkingDirectory, error);
                EXPECT_FALSE(error) << error.message();
            }
            if (m_configFile.empty())
            {
                return;
            }

            // 测试配置位于测试程序旁边，与服务端 bin/config 下的配置相互独立。
            if (m_hadFile)
            {
                utils::FileUtil(m_configFile).setContent(m_originalContent);
            }
            else
            {
                fs::remove(m_configFile, error);
            }
            EXPECT_FALSE(error) << error.message();

            if (!m_hadDirectory)
            {
                fs::remove(m_configFile.parent_path(), error);
                EXPECT_FALSE(error) << error.message();
            }
        }

        static Json::Value validConfig()
        {
            Json::Value root;
            root["server_ip"] = "127.0.0.1";
            root["server_port"] = 8081;
            root["download_prefix"] = "/download/";
            root["deep_storage_dir"] = "./deep_storage/";
            root["low_storage_dir"] = "./low_storage/";
            root["storage_info"] = "./storage.data";
            root["bundle_format"] = bundle::LZIP;
            return root;
        }

        void writeConfig(const Json::Value &root) const
        {
            utils::FileUtil(m_configFile).setContent(Json::writeString(Json::StreamWriterBuilder{}, root));
        }

        static void expectConfig(const Json::Value &root)
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
        std::string m_originalContent;
        bool m_hadDirectory = false;
        bool m_hadFile = false;
    };
}


TEST_F(ConfigTests, LoadsAllSevenFields)
{
    expectConfig(validConfig());
}

TEST_F(ConfigTests, FindsConfigurationBesideExecutableAfterWorkingDirectoryChanges)
{
    fs::current_path(fs::temp_directory_path());

    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    expectConfig(validConfig());
}

TEST_F(ConfigTests, ReloadUpdatesAllFields)
{
    auto root = validConfig();
    root["server_ip"] = "0.0.0.0";
    root["server_port"] = 9090;
    root["download_prefix"] = "/files/";
    root["deep_storage_dir"] = "./data/compressed/";
    root["low_storage_dir"] = "./data/normal/";
    root["storage_info"] = "./data/metadata.json";
    root["bundle_format"] = bundle::RAW;
    writeConfig(root);

    ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
    expectConfig(root);
}

TEST_F(ConfigTests, RejectsMalformedOrNonObjectDocuments)
{
    for (const char *content : {"", "{", "[]", "null", "true", "42"})
    {
        SCOPED_TRACE(content);
        utils::FileUtil(m_configFile).setContent(content);
        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        expectConfig(validConfig());
    }
}

TEST_F(ConfigTests, RejectsMissingFieldsBeforeUpdatingMembers)
{
    for (const char *field : {"server_ip", "server_port", "download_prefix", "deep_storage_dir", "low_storage_dir", "storage_info", "bundle_format"})
    {
        SCOPED_TRACE(field);
        auto root = validConfig();
        root["server_ip"] = "0.0.0.0";
        root.removeMember(field);
        writeConfig(root);

        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        expectConfig(validConfig());
    }
}

TEST_F(ConfigTests, RejectsEmptyOrNonStringFields)
{
    for (const char *field : {"server_ip", "download_prefix", "deep_storage_dir", "low_storage_dir", "storage_info"})
    {
        SCOPED_TRACE(field);
        for (const auto &value : {Json::Value(), Json::Value(""), Json::Value(42), Json::Value(false)})
        {
            auto root = validConfig();
            root[field] = value;
            writeConfig(root);

            EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
            expectConfig(validConfig());
        }
    }
}

TEST_F(ConfigTests, RejectsInvalidPortsAndCompressionFormats)
{
    const struct
    {
        const char *field;
        Json::Value value;
    } cases[] = {
        {"server_port", 0}, {"server_port", -1}, {"server_port", 65536}, {"server_port", "8081"}, {"server_port", true}, {"server_port", 1.5}, {"bundle_format", -1}, {"bundle_format", bundle::BZIP2 + 1}, {"bundle_format", "4"}, {"bundle_format", false}, {"bundle_format", 1.5}};

    for (const auto &item : cases)
    {
        SCOPED_TRACE(item.field);
        auto root = validConfig();
        root[item.field] = item.value;
        writeConfig(root);

        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        expectConfig(validConfig());
    }
}

TEST_F(ConfigTests, AcceptsPortBoundariesAndSupportedCompressionFormats)
{
    for (int port : {1, 65535})
    {
        for (int format : {bundle::RAW, bundle::LZIP, bundle::BZIP2})
        {
            auto root = validConfig();
            root["server_port"] = port;
            root["bundle_format"] = format;
            writeConfig(root);

            ASSERT_NO_THROW(server::Config::GetInstance().readConfig());
            expectConfig(root);
        }
    }
}

TEST_F(ConfigTests, RejectsDownloadPrefixesWithoutLeadingOrTrailingSlash)
{
    for (const char *prefix : {"download/", "/download", "download"})
    {
        auto root = validConfig();
        root["download_prefix"] = prefix;
        writeConfig(root);

        EXPECT_THROW(server::Config::GetInstance().readConfig(), std::invalid_argument);
        expectConfig(validConfig());
    }
}

TEST_F(ConfigTests, MissingFileReportsTheFileErrorAndKeepsLoadedConfiguration)
{
    fs::remove(m_configFile);

    EXPECT_THROW(server::Config::GetInstance().readConfig(), fs::filesystem_error);
    expectConfig(validConfig());
}
