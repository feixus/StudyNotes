require "utility"

ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"

workspace (ENGINE_NAME)
    basedir (ROOT)
    configurations { "Debug", "Release" }
    platforms { "x64" }
    startproject (ENGINE_NAME)
    language "C++"
	cppdialect "C++20"
    defines { "x64" }
    architecture ("x64")
    symbols ("On")
	--kind ("ConsoleApp")
	kind ("WindowedApp")
    characterset ("MBCS")
	flags { "MultiProcessorCompile", "ShadowedVariables" }
	rtti "Off"

	filter "configurations:Debug"
		defines { "_DEBUG" }
		optimize ("Off")
		inlining "Explicit"

	filter "configurations:Release"
		defines { "RELEASE" }
		optimize ("Full")
        linktimeoptimization "On"  -- LTO
        flags { "NoIncrementalLink" }

    filter {}

project (ENGINE_NAME)
    location (ROOT .. ENGINE_NAME)
    targetdir (ROOT .. "Build/$(ProjectName)_$(Platform)_$(Configuration)")
	objdir (ROOT .. "Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")

    -- Precompiled header
    pchheader "stdafx.h"
    pchsource (ROOT .. ENGINE_NAME .. "/stdafx.cpp")
    includedirs { "$(ProjectDir)" }

	SetPlatformDefines()

    -- Windows SDK & toolset (Visual Studio only)
    filter {"system:windows", "action:vs*"}
        --toolset "v143"
    filter {}


    -- Files to include
    files {
        SOURCE_DIR .. "**.h",
        SOURCE_DIR .. "**.hpp",
        SOURCE_DIR .. "**.cpp",
        SOURCE_DIR .. "**.inl",
        SOURCE_DIR .. "**.c",
        SOURCE_DIR .. "**.natvis",
    }

    filter ("files:" .. SOURCE_DIR .. "External/**")
			flags { "NoPCH" }
	filter {}

    postbuildcommands { "{COPY} \"$(ProjectDir)Resources\" \"$(OutDir)Resources\"" }

    ---- External libraries ----
	AddAssimp()
	filter "system:Windows"
		AddD3D12()
		AddPix()
		AddDxc()