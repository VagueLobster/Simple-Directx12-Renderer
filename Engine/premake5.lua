project "Engine"
	kind "WindowedApp"
	language "C++"
	cppdialect "C++20"
	staticruntime "off"
	
	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"src/**.h",
		"src/**.cpp",
		"vendor/glm/glm/**.hpp",
		"vendor/glm/glm/**.inl",
		"vendor/crosswindow-graphics/src/**.h"
	}

	defines
	{
		"WIN32",
		"_WINDOWS",
		"_CRT_SECURE_NO_WARNINGS",
		"_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING",
		"XWIN_WIN32=1",
		"XGFX_DIRECTX12=1",
	}

	includedirs
	{
		"src",
		"%{IncludeDir.glm}",
		"%{IncludeDir.crosswindow}",
		"%{IncludeDir.crosswindow_graphics}",
	}
	
	links
	{
		"CrossWindow"
	}
	
	filter "system:windows"
		systemversion "latest"
		
		links
		{
			"%{Library.WinSock}",
			"%{Library.WinMM}",
			"%{Library.WinVersion}",
			"%{Library.BCrypt}",
			"%{Library.Dbghelp}",
		}

	filter "configurations:Debug"
		defines "SDR_DEBUG"
		runtime "Debug"
		symbols "on"
		inlining ("Auto")
		editandcontinue "Off"
		

	filter "configurations:Release"
		defines "SDR_RELEASE"
		runtime "Release"
		optimize "on"
		inlining ("Auto")

	filter "configurations:Dist"
		defines "SDR_DIST"
		runtime "Release"
		optimize "on"
		inlining ("Auto")