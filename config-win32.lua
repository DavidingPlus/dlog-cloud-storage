function define_win32_options()
    option("suppress_w3_warnings")
        set_default(true)
        set_showmenu(true)
        set_description("Use /W2 instead of the default MSVC warning level.")
    option_end()
end

function apply_win32_target_config()
    -- /Zc:__cplusplus：让 MSVC 的 __cplusplus 宏反映当前启用的 C++ 标准版本。若不启用，MSVC 默认仍会报告旧值，可能会导致意料外的问题。
    add_cxflags("/utf-8", "/Zc:__cplusplus")

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
