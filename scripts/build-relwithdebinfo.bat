@echo off
cd /d "%~dp0.."
if not defined VCPKG_ROOT (
    echo VCPKG_ROOT is not set. Point it at your vcpkg folder, e.g. setx VCPKG_ROOT C:\vcpkg
    pause
    exit /b 1
)
cmake --preset x64-relwithdebinfo && cmake --build out/build/x64-relwithdebinfo --config RelWithDebInfo
pause
