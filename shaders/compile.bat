@echo off
"D:\VulkanSDK\Bin\glslc.exe" shader.vert -o vert.spv
if %ERRORLEVEL% neq 0 exit /b 1
"D:\VulkanSDK\Bin\glslc.exe" shader.frag -o frag.spv
if %ERRORLEVEL% neq 0 exit /b 1
echo Shaders compiled successfully.
