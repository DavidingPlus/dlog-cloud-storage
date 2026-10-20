#include "server/config.h"

#include <stdexcept>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


namespace
{
    constexpr const char *kConfigFile = "config/server.conf";
}


Config::Config()
{
    if (!readConfig()) throw std::runtime_error("Config::Config(): readConfig() failed");
}

Config &Config::GetInstance()
{
    // 详见：https://github.com/DavidingPlus/dfiber/tree/master/snippet/Singleton
    static Config instance;
    return instance;
}

bool Config::readConfig()
{
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
