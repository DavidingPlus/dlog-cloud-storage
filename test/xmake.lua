if not get_config("with_gtest") then
    return
end

includes("config.lua")


add_requires("gtest")


-- 测试分组的唯一清单，每项对应 test 下的一个目录。common 对所有平台生效；win32 和 linux 分别只在 Windows 和 Linux 目标平台生效。
local gtest_groups = {
    common = {"testrun"},
    win32 = {},
    linux = {"server", "third_party"}
}

local active_gtest_groups = {}

local function enable_gtest_groups(groups)
    for _, group in ipairs(groups) do
        table.insert(active_gtest_groups, group)
    end
end

enable_gtest_groups(gtest_groups.common)

if is_plat("windows") then
    enable_gtest_groups(gtest_groups.win32)
elseif is_plat("linux") then
    enable_gtest_groups(gtest_groups.linux)
end


-- 请用 xmake test 执行测试。
target("tests")
    set_default(false)

    set_kind("binary")
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test")

    add_packages("gtest")

    if is_plat("linux") then
        add_deps("dlog-cloud-storage-utils-static")
        add_packages("bundle", "cpp-base64", "jsoncpp")
    end

    before_build(function ()
        os.tryrm("$(builddir)/$(plat)/$(arch)/$(mode)/test/gtest")
        os.mkdir("$(builddir)/$(plat)/$(arch)/$(mode)/test/gtest")
    end)

    on_install(function () end)
    apply_current_platform_target_config()

    add_files("main.cpp")
    apply_gtest_summary_config("tests", active_gtest_groups)

    for _, group in ipairs(active_gtest_groups) do
        add_tests(group, {
            realtime_output = true,
            files = {group .. "/*.cpp"}
        })
    end
target_end()
