function SetPlatformDefines()
	filter "system: windows"
		defines { "PLATFORM_WINDOWS" }
	filter {}
end

function AddPix()
	filter {}
	includedirs (ROOT .. "D3D12/External/Pix/include")
	libdirs (ROOT .. "D3D12/External/Pix/lib")
	postbuildcommands { ("copy \"$(SolutionDir)D3D12/External\\Pix\\bin\\WinPixEventRuntime.dll\" \"$(OutDir)\"") }
	links { "WinPixEventRuntime" }
end

function AddD3D12()
	filter {}
	links {	"d3d12.lib", "dxgi", "d3dcompiler", "dxguid" }
end

function AddAssimp()
	filter {}
	includedirs (ROOT .. "D3D12/External/Assimp/include")
	libdirs	(ROOT .. "D3D12/External/Assimp/lib/x64")
	postbuildcommands { ("copy \"$(SolutionDir)D3D12/External\\Assimp\\bin\\x64\\assimp-vc143-mt.dll\" \"$(OutDir)\"") }
	links { "assimp-vc143-mt" }
end

function AddDxc()
	links { "dxcompiler" }
	includedirs (ROOT .. "D3D12/External/Dxc")
	postbuildcommands { ("copy \"$(SolutionDir)D3D12/External\\Dxc\\dxcompiler.dll\" \"$(OutDir)\"") }
	postbuildcommands { ("copy \"$(SolutionDir)D3D12/External\\Dxc\\dxil.dll\" \"$(OutDir)\"") }
end

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