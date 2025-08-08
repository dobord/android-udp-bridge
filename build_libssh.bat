@echo off
setlocal ENABLEDELAYEDEXPANSION

echo [INFO] Building prebuilt libssh and mbedTLS for Android (Windows)...

REM Resolve repository root and target install dirs
set "REPO_DIR=%~dp0"
set "PREBUILT_DIR=%REPO_DIR%ssh-tunnel-android-app\app\src\main\prebuilt"

REM Check SDK/NDK location (prefer env, then common locations)
if not defined ANDROID_HOME (
  if exist "E:\Android\Sdk" (
    set "ANDROID_HOME=E:\Android\Sdk"
  ) else (
    if exist "%LOCALAPPDATA%\Android\Sdk" (
      set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
    )
  )
)

REM Prefer specific NDK version used by the project
set "NDK_VERSION_PREF=25.1.8937393"
if not defined ANDROID_NDK_HOME (
  if defined ANDROID_HOME if exist "%ANDROID_HOME%\ndk\%NDK_VERSION_PREF%" (
    set "ANDROID_NDK_HOME=%ANDROID_HOME%\ndk\%NDK_VERSION_PREF%"
  )
)

REM Fallback: pick any NDK folder (last enumerated)
if not defined ANDROID_NDK_HOME (
  if defined ANDROID_HOME if exist "%ANDROID_HOME%\ndk" (
    for /d %%f in ("%ANDROID_HOME%\ndk\*") do set "ANDROID_NDK_HOME=%%f"
  ) else (
    if exist "E:\Android\Sdk\ndk" (
      for /d %%f in ("E:\Android\Sdk\ndk\*") do set "ANDROID_NDK_HOME=%%f"
    )
  )
)

if not defined ANDROID_NDK_HOME (
  echo [ERROR] ANDROID_NDK_HOME is not set and NDK not found automatically.
  echo         Please install NDK via Android SDK Manager and set ANDROID_HOME/ANDROID_NDK_HOME.
  exit /b 1
)

if not exist "%ANDROID_NDK_HOME%\toolchains" (
  echo [ERROR] Invalid ANDROID_NDK_HOME: %ANDROID_NDK_HOME%
  exit /b 1
)

echo [INFO] ANDROID_NDK_HOME=%ANDROID_NDK_HOME%

REM Add CMake from Android SDK to PATH if available
if defined ANDROID_HOME if exist "%ANDROID_HOME%\cmake" (
  for /d %%f in ("%ANDROID_HOME%\cmake\*") do set "CMAKE_LAST=%%~nxf"
  if defined CMAKE_LAST set "CMAKE_BIN=%ANDROID_HOME%\cmake\%CMAKE_LAST%\bin"
  if defined CMAKE_BIN if exist "%CMAKE_BIN%\cmake.exe" set "PATH=%CMAKE_BIN%;%PATH%"
)

REM Determine parallel jobs
set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=4"

REM Normalize NDK path to forward slashes for CMake to avoid escape issues
set "ANDROID_NDK_HOME_UNIX=%ANDROID_NDK_HOME:\=/%"
set "NDK_TOOLCHAIN_FILE=%ANDROID_NDK_HOME_UNIX%/build/cmake/android.toolchain.cmake"

set "LIBSSH_VERSION=0.11.2"
set "MBEDTLS_VERSION=2.28.7"
set "MIN_API_LEVEL=24"
set "ABIS=arm64-v8a"

set "WORK_DIR=%REPO_DIR%libssh_build"
set "MBEDTLS_WORK_DIR=%WORK_DIR%\mbedtls"
set "MBEDTLS_SOURCE_DIR=%MBEDTLS_WORK_DIR%\mbedtls-%MBEDTLS_VERSION%"
set "MBEDTLS_TARBALL=%MBEDTLS_WORK_DIR%\mbedtls-%MBEDTLS_VERSION%.tar.gz"
set "LIBSSH_TARBALL=%WORK_DIR%\libssh-%LIBSSH_VERSION%.tar.xz"
set "LIBSSH_SOURCE_DIR=%WORK_DIR%\libssh-%LIBSSH_VERSION%"

if not exist "%WORK_DIR%" mkdir "%WORK_DIR%"
if not exist "%PREBUILT_DIR%" mkdir "%PREBUILT_DIR%"

REM Require curl and tar for downloads/extracts (Git for Windows provides tar)
where curl >nul 2>nul
if errorlevel 1 (
  echo [ERROR] curl not found in PATH. Please install Git for Windows or add curl to PATH.
  exit /b 1
)
where tar >nul 2>nul
if errorlevel 1 (
  echo [ERROR] tar not found in PATH. Please install Git for Windows ^(includes tar^) or add tar to PATH.
  exit /b 1
)

echo [INFO] Downloading mbedTLS if needed...
if not exist "%MBEDTLS_TARBALL%" (
  mkdir "%MBEDTLS_WORK_DIR%" 2>nul
  pushd "%MBEDTLS_WORK_DIR%"
  curl -L -o "mbedtls-%MBEDTLS_VERSION%.tar.gz" "https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v%MBEDTLS_VERSION%.tar.gz"
  if errorlevel 1 goto :error
  call :extract_archive "mbedtls-%MBEDTLS_VERSION%.tar.gz"
  if errorlevel 1 goto :error
  popd
)
if not exist "%MBEDTLS_SOURCE_DIR%" (
  pushd "%MBEDTLS_WORK_DIR%"
  if exist "mbedtls-%MBEDTLS_VERSION%.tar.gz" call :extract_archive "mbedtls-%MBEDTLS_VERSION%.tar.gz"
  popd
)
if not exist "%MBEDTLS_SOURCE_DIR%" (
  for /d %%d in ("%MBEDTLS_WORK_DIR%\mbedtls-*") do set "MBEDTLS_SOURCE_DIR=%%d"
)

echo [INFO] Downloading libssh if needed...
if not exist "%LIBSSH_TARBALL%" (
  pushd "%WORK_DIR%"
  curl -L -o "libssh-%LIBSSH_VERSION%.tar.xz" "https://www.libssh.org/files/0.11/libssh-%LIBSSH_VERSION%.tar.xz"
  if errorlevel 1 goto :error
  call :extract_archive "libssh-%LIBSSH_VERSION%.tar.xz"
  if errorlevel 1 goto :error
  popd
)
if not exist "%LIBSSH_SOURCE_DIR%" (
  pushd "%WORK_DIR%"
  if exist "libssh-%LIBSSH_VERSION%.tar.xz" call :extract_archive "libssh-%LIBSSH_VERSION%.tar.xz"
  popd
)
if not exist "%LIBSSH_SOURCE_DIR%" (
  for /d %%d in ("%WORK_DIR%\libssh-*") do set "LIBSSH_SOURCE_DIR=%%d"
)

for %%A in (%ABIS%) do call :build_for_abi %%A
if errorlevel 1 goto :error

echo.
echo [SUCCESS] libssh build completed.
echo [INFO] Installed to: %PREBUILT_DIR%\libssh\
exit /b 0

:extract_archive
setlocal
set "ARCHIVE=%~1"
where cmake >nul 2>nul
if not errorlevel 1 (
  echo [INFO] Extracting with CMake: %ARCHIVE%
  cmake -E tar xvf "%ARCHIVE%"
  if errorlevel 1 (
    echo [WARN] CMake extraction failed, trying tar...
    tar -xf "%ARCHIVE%"
  )
) else (
  echo [INFO] Extracting with tar: %ARCHIVE%
  tar -xf "%ARCHIVE%"
)
if errorlevel 1 (
  echo [ERROR] Failed to extract: %ARCHIVE%
  endlocal & exit /b 1
)
endlocal & exit /b 0

:build_for_abi
setlocal
set "ABI=%~1"
set "ANDROID_ABI=%ABI%"
set "BUILD_BASE_DIR=%WORK_DIR%\build"
set "BUILD_DIR=%BUILD_BASE_DIR%\%ABI%"
set "INSTALL_DIR=%PREBUILT_DIR%\libssh\%ABI%"
set "MBEDTLS_BUILD_BASE_DIR=%MBEDTLS_WORK_DIR%\build"
set "MBEDTLS_BUILD_DIR=%MBEDTLS_BUILD_BASE_DIR%\%ABI%"
set "MBEDTLS_INSTALL_DIR=%PREBUILT_DIR%\mbedtls\%ABI%"

if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%MBEDTLS_BUILD_DIR%" rmdir /s /q "%MBEDTLS_BUILD_DIR%"
mkdir "%BUILD_DIR%" 2>nul
mkdir "%INSTALL_DIR%" 2>nul
mkdir "%MBEDTLS_BUILD_DIR%" 2>nul
mkdir "%MBEDTLS_INSTALL_DIR%" 2>nul

echo [INFO] Building mbedTLS for %ABI%...
pushd "%MBEDTLS_BUILD_DIR%"
cmake -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE="%NDK_TOOLCHAIN_FILE%" ^
  "%MBEDTLS_SOURCE_DIR%" ^
  -DANDROID_ABI=%ANDROID_ABI% ^
  -DANDROID_PLATFORM=android-%MIN_API_LEVEL% ^
  -DANDROID_STL=c++_shared ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX="%MBEDTLS_INSTALL_DIR%" ^
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON ^
  -DCMAKE_C_FLAGS="-fPIC" ^
  -DCMAKE_CXX_FLAGS="-fPIC" ^
  -DENABLE_TESTING=OFF ^
  -DENABLE_PROGRAMS=OFF ^
  -DBUILD_SHARED_LIBS=OFF
if errorlevel 1 goto :error_pop
cmake --build . --config Release -- -j %JOBS%
if errorlevel 1 goto :error_pop
cmake --install .
if errorlevel 1 goto :error_pop
popd

echo [INFO] Building libssh for %ABI%...
pushd "%BUILD_DIR%"
cmake -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE="%NDK_TOOLCHAIN_FILE%" ^
  "%LIBSSH_SOURCE_DIR%" ^
  -DANDROID_ABI=%ANDROID_ABI% ^
  -DANDROID_PLATFORM=android-%MIN_API_LEVEL% ^
  -DANDROID_STL=c++_shared ^
  -DCMAKE_C_FLAGS="-DS_IWRITE=S_IWUSR" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON ^
  -DWITH_EXAMPLES=OFF ^
  -DWITH_TESTING=OFF ^
  -DWITH_SERVER=OFF ^
  -DWITH_ZLIB=OFF ^
  -DWITH_GSSAPI=OFF ^
  -DWITH_PCAP=OFF ^
  -DWITH_SFTP=ON ^
  -DWITH_MBEDTLS=ON ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DMBEDTLS_ROOT_DIR="%MBEDTLS_INSTALL_DIR%" ^
  -DMBEDTLS_INCLUDE_DIR="%MBEDTLS_INSTALL_DIR%/include" ^
  -DMBEDTLS_CRYPTO_LIBRARY="%MBEDTLS_INSTALL_DIR%/lib/libmbedcrypto.a" ^
  -DMBEDTLS_X509_LIBRARY="%MBEDTLS_INSTALL_DIR%/lib/libmbedx509.a" ^
  -DMBEDTLS_SSL_LIBRARY="%MBEDTLS_INSTALL_DIR%/lib/libmbedtls.a"
if errorlevel 1 goto :error_pop
cmake --build . --config Release -- -j %JOBS%
if errorlevel 1 goto :error_pop
cmake --install .
if errorlevel 1 goto :error_pop
popd

echo [OK] %ABI% done.
endlocal & goto :eof

:error_pop
set ERR=%ERRORLEVEL%
popd
exit /b %ERR%

:error
set ERR=%ERRORLEVEL%
echo [ERROR] Build failed with exit code %ERR%.
exit /b %ERR%
