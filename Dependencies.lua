-- Simple D3D12 Renderer Dependencies

IncludeDir = {}
IncludeDir["glm"] = "%{wks.location}/Engine/vendor/glm"
IncludeDir["crosswindow"] = "%{wks.location}/Engine/vendor/crosswindow/src"
IncludeDir["crosswindow_graphics"] = "%{wks.location}/Engine/vendor/crosswindow-graphics/src"

Library = {}

-- Windows
Library["WinSock"] = "Ws2_32.lib"
Library["WinMM"] = "Winmm.lib"
Library["WinVersion"] = "Version.lib"
Library["BCrypt"] = "Bcrypt.lib"
Library["Dbghelp"] = "Dbghelp.lib"