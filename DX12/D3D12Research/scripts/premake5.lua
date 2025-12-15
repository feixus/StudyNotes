ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"

workspace (ENGINE_NAME)
    basedir (ROOT)
    configurations { "Debug", "Release", "DebugASAN" }
    platforms { "x64" }
    startproject (ENGINE_NAME)
    language "C++"
	cppdialect "C++20"
    defines { "x64" }
    architecture "x64"
    symbols "On"
	kind "WindowedApp"
    characterset "MBCS"
	flags { "MultiProcessorCompile", "ShadowedVariables"}
	rtti "Off"
    conformancemode "On"
    warnings "Extra"

    disablewarnings { "4100" }

	filter "configurations:Debug"
        runtime "Debug"
		defines { "_DEBUG" }
		optimize ("Off")
		inlining "Explicit"

	filter "configurations:Release"
        runtime "Release"
		defines { "RELEASE" }
		optimize ("Full")
        linktimeoptimization "On"  -- LTO
        flags { "NoIncrementalLink" }

    filter "configurations:DebugASAN"
        runtime "Release" 
		optimize "Off"
		symbols "On"
		sanitize { "Address" }
		flags { "NoIncrementalLink", "NoMinimalRebuild", "NoRuntimeChecks" }
		editandcontinue "Off"
		
		buildoptions { "-fsanitize=address" } 
    	linkoptions  { "-fsanitize=address" }

		toolset "clang"
		
		-- libdirs {
        -- 	"D:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/lib/clang/17/lib/windows"
    	-- }

    filter {}

project (ENGINE_NAME)
    location (ROOT .. ENGINE_NAME)
    targetdir (ROOT .. "Build/$(ProjectName)_$(Platform)_$(Configuration)")
	objdir (ROOT .. "Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")

    -- Precompiled header
    pchheader "stdafx.h"
    pchsource (ROOT .. ENGINE_NAME .. "/stdafx.cpp")
    includedirs { "$(ProjectDir)" }
	
	includedirs (ROOT .. "D3D12/External")
	includedirs (ROOT .. "D3D12/Resources/Shaders/Interop")

    -- Windows SDK & toolset (Visual Studio only)
    filter {"system:windows", "action:vs*"}
        --toolset "v143"
		defines { "PLATFORM_WINDOWS" }
    filter {}


    -- Files to include
    files {
        SOURCE_DIR .. "**.h",
        SOURCE_DIR .. "**.hpp",
        SOURCE_DIR .. "**.cpp",
        SOURCE_DIR .. "**.inl",
        SOURCE_DIR .. "**.c",
        SOURCE_DIR .. "**.natvis",
        SOURCE_DIR .. "**.editorconfig",
		SOURCE_DIR .. "Resources/Shaders/Interop/**",
		--SOURCE_DIR .. "**.hlsl*",
    }
	
	vpaths
	{
		--{["Shaders/Include"] = (SOURCE_DIR .. "**.hlsli")},
		--{["Shaders/Source"] = (SOURCE_DIR .. "**.hlsl")},
		{["Shaders/Interop"] = (SOURCE_DIR .. "**/Interop/**.h")}
	}

    filter ("files:" .. SOURCE_DIR .. "External/**")
			flags { "NoPCH" }
            fatalwarnings { }
            warnings "Default"
	filter {}

    postbuildcommands { "{COPY} \"$(ProjectDir)Resources\" \"$(OutDir)Resources\"" }

    ---- External libraries ----
	filter "system:Windows"
		-- D3D12
		links {	"d3d12.lib", "dxgi", "d3dcompiler", "dxguid" }
	
		includedirs (ROOT .. "D3D12/External/D3D12/include")
		includedirs (ROOT .. "D3D12/External/D3D12/include/d3dx12")
		postbuildcommands { ("{COPY} \"$(SolutionDir)D3D12/External\\D3D12\\bin\\D3D12Core.dll\" \"$(OutDir)\\D3D12\\\"") }
		postbuildcommands { ("{COPY} \"$(SolutionDir)D3D12/External\\D3D12\\bin\\d3d12SDKLayers.dll\" \"$(OutDir)\\D3D12\\\"") }

		-- pix
		includedirs (ROOT .. "D3D12/External/Pix/include")
		libdirs (ROOT .. "D3D12/External/Pix/lib")
		postbuildcommands { ("{COPY} \"$(SolutionDir)D3D12/External\\Pix\\bin\\WinPixEventRuntime.dll\" \"$(OutDir)\"") }
		links { "WinPixEventRuntime" }

		-- dxc
		links { "dxcompiler" }
		includedirs (ROOT .. "D3D12/External/Dxc")
		libdirs	(ROOT .. "D3D12/External/Dxc")
		postbuildcommands { ("{COPY} \"$(SolutionDir)D3D12/External\\Dxc\\dxcompiler.dll\" \"$(OutDir)\"") }
		postbuildcommands { ("{COPY} \"$(SolutionDir)D3D12/External\\Dxc\\dxil.dll\" \"$(OutDir)\"") }

		-- optick
		links { "OptickCore" }
		includedirs (ROOT .. "D3D12/External/Optick/include")
		libdirs	(ROOT .. "D3D12/External/Optick/lib")
		postbuildcommands { ("{COPY} \"$(SolutionDir)D3D12/External\\Optick\\bin\\OptickCore.dll\" \"$(OutDir)\"") }
		
newaction {
	trigger     = "clean",
	description = "Remove all binaries and generated files",

	execute = function()
		os.rmdir("../Build")
		os.rmdir("../ipch")
		os.rmdir("../.vs")
		os.remove("../*.sln")
		os.remove(SOURCE_DIR .. "*.vcxproj.*")
	end
}