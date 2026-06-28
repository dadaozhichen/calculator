add_rules("plugin.compile_commands.autoupdate")
add_rules("mode.release", "mode.debug")

set_languages("c++20")

-- Use Homebrew LLVM on macOS, default toolchain on other platforms.
if is_os("macosx") then
    set_config("sdk", "/opt/homebrew/opt/llvm")
    set_toolchains("llvm")
end

add_requires("imgui", "libsdl3")

target("calculator")
    set_kind("binary")
    add_files("src/main.cpp")
    add_files("src/imgui_impl_sdl3.cpp")
    add_files("src/imgui_impl_opengl3.cpp")
    add_packages("imgui", "libsdl3")

    -- macOS needs the OpenGL framework.
    if is_os("macosx") then
        add_defines("GL_SILENCE_DEPRECATION")
        add_frameworks("OpenGL")
    end

    -- Windows links OpenGL via the system library.
    if is_os("windows") then
        add_syslinks("opengl32")
    end

    set_installdir("bin")
