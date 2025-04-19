-- Engine name and root path
engineName = "D3D12"
ROOT = "../"

workspace (engineName)
    basedir (ROOT)
    configurations { "Debug", "Development", "Shipping" }
    platforms { "x32", "x64" }
    startproject (engineName)
    language "C++"
	cppdialect "C++20"
    defines { "_CONSOLE", "THREADING", "PLATFORM_WINDOWS" }
    kind "WindowedApp"

    -- Platform-specific defines
    filter { "platforms:x64" }
        defines { "x64", "_AMD64_" }

    filter { "platforms:x32" }
        defines { "x32", "_X86" }

    -- Configuration-specific settings
    filter "configurations:Debug"
        defines { "_DEBUG" }
        symbols "On"
        warnings "Extra"

    filter "configurations:Development"
        defines { "DEVELOPMENT" }
        optimize "Speed"
        symbols "On"
        warnings "Extra"

    filter "configurations:Shipping"
        defines { "SHIPPING" }
        optimize "Speed"
        flags { "No64BitChecks" }

    -- Reset filter
    filter {}

project (engineName)
    location (ROOT .. engineName)
    targetdir (ROOT .. "Build/" .. engineName .. "_%{platform}_%{cfg.buildcfg}")
    objdir (ROOT .. "Build/Intermediate/%{prj.name}_%{platform}_%{cfg.buildcfg}")

    -- Precompiled header
    pchheader "stdafx.h"
    pchsource (ROOT .. engineName .. "/stdafx.cpp")

    -- Windows SDK & toolset (Visual Studio only)
    filter "action:vs*"
        systemversion "10.0.22621.0"
        toolset "v143"

    -- Files to include
    files {
        ROOT .. "**.h",
        ROOT .. "**.hpp",
        ROOT .. "**.cpp",
        ROOT .. "**.inl",
        ROOT .. "**.c",
        ROOT .. "**.natvis",
    }

    -- Include directories
    includedirs {
        "$(ProjectDir)"
    }

    -- Libraries to link
    links {
        "dxgi",
        "d3dcompiler"
    }

    -- Reset filter
    filter {}
