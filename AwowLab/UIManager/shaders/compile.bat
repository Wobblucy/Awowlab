@echo off
echo Compiling shaders to SPIR-V...

glslc sphere.vert -o sphere_vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile sphere.vert
    exit /b 1
)

glslc sphere.frag -o sphere_frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile sphere.frag
    exit /b 1
)

glslc floor.vert -o floor_vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile floor.vert
    exit /b 1
)

glslc floor.frag -o floor_frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile floor.frag
    exit /b 1
)

glslc selection_ring.vert -o selection_ring_vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile selection_ring.vert
    exit /b 1
)

glslc selection_ring.frag -o selection_ring_frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile selection_ring.frag
    exit /b 1
)

glslc classicon.vert -o classicon_vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile classicon.vert
    exit /b 1
)

glslc classicon.frag -o classicon_frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile classicon.frag
    exit /b 1
)

glslc textured_model.vert -o textured_model_vert.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile textured_model.vert
    exit /b 1
)

glslc textured_model.frag -o textured_model_frag.spv
if %ERRORLEVEL% NEQ 0 (
    echo Failed to compile textured_model.frag
    exit /b 1
)

echo Shader compilation successful!
