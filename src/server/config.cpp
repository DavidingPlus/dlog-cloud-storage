#include "server/config.h"

#include <stdexcept>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


namespace
{
    constexpr const char *ConfigFile = "config/Storage.conf";
}


Config::Config()
{
    if (!ReadConfig()) throw std::runtime_error("Config::Config(): ReadConfig() failed");
}

Config &Config::GetInstance()
{
    // 详见：https://github.com/DavidingPlus/dfiber/tree/master/snippet/Singleton
    static Config instance;
    return instance;
}

bool Config::ReadConfig()
{
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
