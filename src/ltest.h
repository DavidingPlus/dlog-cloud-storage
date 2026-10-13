/**
 * @file ltest.h
 * @author DavidingPlus (davidingplus@qq.com)
 * @brief 测试类头文件。
 *
 * Copyright (c) 2024 电子科技大学 刘治学
 *
 */

#ifndef _XMAKE_PROJECT_LTEST_H_
#define _XMAKE_PROJECT_LTEST_H_

#include "globalmacros.h"

#include <string>


XMAKE_PROJECT_NAMESPACE_BEGIN(xmake_project)


class XMAKE_PROJECT_API_EXPORTED LTest
{

public:

    static std::string foo() { return std::string("hello world"); }

    std::pair<int, int> gee(int first, int second) const;
};


XMAKE_PROJECT_NAMESPACE_END


#endif
