include "Dependencies.lua"

workspace "SimpleDirectX12Renderer"
	conformancemode "On"
	startproject "Engine"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

	flags
	{
		"MultiProcessorCompile"
	}
	
	filter "language:C++ or language:C"
		architecture "x86_64"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

group "Dependencies"
	include "Engine/vendor/crosswindow"
group ""

group "Core"
	include "Engine"
group ""
