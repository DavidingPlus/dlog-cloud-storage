#include "util.h"

#include <stdexcept>


DLOG_CLOUD_STORAGE_NAMESPACE_BEGIN(server)


unsigned char toHex(unsigned char x)
{
    if (x > 15)
    {
        throw std::invalid_argument("hex value must be between 0 and 15");
    }

    return static_cast<unsigned char>(x < 10 ? '0' + x : 'A' + x - 10);
}

unsigned char fromHex(unsigned char x)
{
    if (x >= '0' && x <= '9')
    {
        return static_cast<unsigned char>(x - '0');
    }
    if (x >= 'A' && x <= 'F')
    {
        return static_cast<unsigned char>(x - 'A' + 10);
    }
    if (x >= 'a' && x <= 'f')
    {
        return static_cast<unsigned char>(x - 'a' + 10);
    }

    throw std::invalid_argument("invalid hexadecimal character");
}

std::string urlDecode(const std::string &str)
{
    std::string decoded;
    decoded.reserve(str.size());

    for (std::size_t i = 0; i < str.size(); ++i)
    {
        if (str[i] != '%')
        {
            decoded.push_back(str[i]);
            continue;
        }

        if (str.size() - i < 3)
        {
            throw std::invalid_argument("incomplete URL percent escape");
        }

        const unsigned char high = fromHex(static_cast<unsigned char>(str[i + 1]));
        const unsigned char low = fromHex(static_cast<unsigned char>(str[i + 2]));
        decoded.push_back(static_cast<char>(high * 16 + low));
        i += 2;
    }

    return decoded;
}


DLOG_CLOUD_STORAGE_NAMESPACE_END
