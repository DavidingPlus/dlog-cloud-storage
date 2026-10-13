#include <gtest/gtest.h>

#include <string>

#include "ltest.h"


using namespace dlog_cloud_storage;


TEST(TestClassTest, Test1)
{
    EXPECT_EQ(LTest::foo(), std::string("hello world"));
}

TEST(TestClassTest, Test2)
{
    std::pair<int, int> p(3, 4);

    EXPECT_EQ(LTest().gee(3, 4), p);
}
