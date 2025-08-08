@echo off
setlocal ENABLEDELAYEDEXPANSION

echo [INFO] Building SSH Tunnel Android App (Windows)...

REM Resolve repository root and app dir
set "REPO_DIR=%~dp0"
set "APP_DIR=%REPO_DIR%ssh-tunnel-android-app"

if not exist "%APP_DIR%\gradlew.bat" (
  echo [ERROR] Gradle wrapper not found at "%APP_DIR%\gradlew.bat".
  echo         Make sure you run this script from the repository root and that the wrapper exists.
  exit /b 1
)

REM Optional: Ensure Android SDK is available (Gradle will also use local.properties)
if not defined ANDROID_HOME (
  if exist "E:\Android\Sdk" (
    set "ANDROID_HOME=E:\Android\Sdk"
  ) else if exist "%LOCALAPPDATA%\Android\Sdk" (
    set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
  )
)

if defined ANDROID_HOME (
  echo [INFO] ANDROID_HOME=%ANDROID_HOME%
) else (
  echo [WARN] ANDROID_HOME is not set. Using sdk.dir from local.properties if available.
)

pushd "%APP_DIR%"

echo [INFO] Cleaning previous builds...
call .\gradlew.bat --no-daemon clean
if errorlevel 1 goto :build_error

echo [INFO] Building debug APK...
call .\gradlew.bat --no-daemon assembleDebug
if errorlevel 1 goto :build_error

echo [SUCCESS] Build completed!
echo [INFO] APK location: %APP_DIR%\app\build\outputs\apk\debug\app-debug.apk
popd
exit /b 0

:build_error
set ERR=%ERRORLEVEL%
echo [ERROR] Build failed with exit code %ERR%.
popd
exit /b %ERR%
