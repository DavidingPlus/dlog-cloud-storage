#include <iostream>

#include "server/config.h"


int main()
{
    auto &config = server::Config::GetInstance();

    std::cout << "server_ip: " << config.getServerIp() << '\n'
              << "server_port: " << config.getServerPort() << '\n'
              << "download_prefix: " << config.getDownloadPrefix() << '\n'
              << "compressed_files_dir: " << config.getCompressedFilesDirPath().string() << '\n'
              << "uncompressed_files_dir: " << config.getUncompressedFilesDirPath().string() << '\n'
              << "metadata_file: " << config.getMetadataFilePath().string() << '\n'
              << "bundle_format: " << config.getBundleFormat()
              << std::endl;
}
