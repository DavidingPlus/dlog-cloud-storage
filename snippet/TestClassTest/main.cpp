#include <iostream>

#include "ltest.h"


int main()
{
    std::cout << dlog_cloud_storage::LTest::foo() << std::endl;

    auto p = dlog_cloud_storage::LTest().gee(3, 4);

    std::cout << p.first << std::endl;
    std::cout << p.second << std::endl;

    return 0;
}
