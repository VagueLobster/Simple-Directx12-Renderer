project "CrossWindow"
	kind "StaticLib"
	language "C++"
	cppdialect "C++14"
	staticruntime "off"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/CrossWindow/CrossWindow.h",
		"src/CrossWindow/Common/*.cpp",
		"src/CrossWindow/Common/*.mm",
		"src/CrossWindow/Common/*.h",
		"src/CrossWindow/Main/Main.h",
		"src/CrossWindow/Win32/**.cpp",
		"src/CrossWindow/Win32/**.mm",
		"src/CrossWindow/Win32/**.h",
		"src/CrossWindow/Main/Win32Main.cpp"
	}

	defines
	{
		"XWIN_WIN32=1"
	}

	includedirs
	{
		"src"
	}

	characterset ("MBCS")

	filter "system:windows"
		systemversion "latest"

	filter "configurations:Debug"
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		runtime "Release"
		optimize "on"

	filter "configurations:Dist"
		runtime "Release"
		optimize "on"
