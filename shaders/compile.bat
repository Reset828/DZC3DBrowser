@echo off
"D:\VulkanSDK\Bin\glslc.exe" 2d.vert -o 2d_vert.spv
if %ERRORLEVEL% neq 0 exit /b 1
"D:\VulkanSDK\Bin\glslc.exe" 2d.frag -o 2d_frag.spv
if %ERRORLEVEL% neq 0 exit /b 1
"D:\VulkanSDK\Bin\glslc.exe" 3d.vert -o 3d_vert.spv
if %ERRORLEVEL% neq 0 exit /b 1
"D:\VulkanSDK\Bin\glslc.exe" 3d.frag -o 3d_frag.spv
if %ERRORLEVEL% neq 0 exit /b 1
echo Shaders compiled successfully.
