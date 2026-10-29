function define_win32_options()
    option("suppress_w3_warnings")
        set_default(true)
        set_showmenu(true)
        set_description("Use /W2 instead of the default MSVC warning level.")
    option_end()
end

function apply_win32_target_config()
    add_cxflags("/utf-8")
    -- UTF8-CPP 根据 __cplusplus 启用 C++17 接口；让 MSVC 报告实际使用的语言标准。
    add_cxxflags("/Zc:__cplusplus")

    -- 只在 debug 模式下添加调试信息。
    if is_mode("debug") then
        set_symbols("debug", "embed")
    end

    -- XMake 默认也是这样设置的。
    -- if get_config("runtimes") == nil then
    --     if is_mode("debug") then
    --         set_runtimes("MDd")
    --     elseif is_mode("release") then
    --         set_runtimes("MD")
    --     end
    -- end

    if get_config("suppress_w3_warnings") ~= false then
        add_cxflags("/W2")
    end
end
