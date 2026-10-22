#include "server/service.h"

#include "server/config.h"


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


// TODO

Service::Service()
{
}

bool Service::runModule()
{
}

void Service::GenHandler(evhttp_request *, void *)
{
}

void Service::Upload(evhttp_request *, void *)
{
}

std::string Service::TimetoStr(std::filesystem::file_time_type)
{
}

std::string Service::GenerateModernFileList(const std::vector<FileMetadata> &)
{
}

std::string Service::FormatSize(std::uint64_t)
{
}

void Service::ListShow(evhttp_request *, void *)
{
}

std::string Service::GetETag(const FileMetadata &)
{
}

void Service::Download(evhttp_request *, void *)
{
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
