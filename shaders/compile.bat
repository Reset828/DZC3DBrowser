@echo off
powershell -Command "Get-ChildItem *.vert,*.frag | ForEach-Object { $bytes = [System.IO.File]::ReadAllBytes($_.FullName); if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) { [System.IO.File]::WriteAllBytes($_.FullName, $bytes[3..($bytes.Length-1)]) } }"
"D:\VulkanSDK\Bin\glslc.exe" 2d.vert -o 2d_vert.spv
if %ERRORLEVEL% neq 0 exit /b 1
"D:\VulkanSDK\Bin\glslc.exe" 2d.frag -o 2d_frag.spv
if %ERRORLEVEL% neq 0 exit /b 1
"D:\VulkanSDK\Bin\glslc.exe" 3d.vert -o 3d_vert.spv
if %ERRORLEVEL% neq 0 exit /b 1
"D:\VulkanSDK\Bin\glslc.exe" 3d.frag -o 3d_frag.spv
if %ERRORLEVEL% neq 0 exit /b 1
echo Shaders compiled successfully.
