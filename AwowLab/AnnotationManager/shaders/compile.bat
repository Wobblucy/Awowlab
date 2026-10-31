@echo off
echo Compiling annotation shaders to SPIR-V...

glslc annotation.vert -o annotation_vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile annotation.vert
    exit /b 1
)

glslc annotation.frag -o annotation_frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile annotation.frag
    exit /b 1
)

echo Annotation shader compilation successful!
