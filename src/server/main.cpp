#include <iostream>

#include "server/config.h"


int main()
{
    auto &config = server::Config::GetInstance();

    std::cout << "server_ip: " << config.getServerIp() << '\n'
              << "server_port: " << config.getServerPort() << '\n'
              << "download_prefix: " << config.getDownloadPrefix() << '\n'
              << "deep_storage_dir: " << config.getDeepStorageDir().string() << '\n'
              << "low_storage_dir: " << config.getLowStorageDir().string() << '\n'
              << "metadata_file: " << config.getMetadataFilePath().string() << '\n'
              << "bundle_format: " << config.getBundleFormat()
              << std::endl;
}
