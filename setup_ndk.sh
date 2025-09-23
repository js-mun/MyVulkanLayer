#!/usr/bin/env bash
set -e

# 프로젝트 내부 경로를 ANDROID_HOME으로 지정
ANDROID_HOME="$(pwd)/External/Android"
NDK_VERSION="26.1.10909125"

echo "Installing Android NDK $NDK_VERSION to $ANDROID_HOME ..."

# cmdline-tools가 없으면 설치
mkdir -p "$ANDROID_HOME/cmdline-tools"
cd "$ANDROID_HOME/cmdline-tools"

if [ ! -d "latest" ]; then
    echo "Downloading Android commandline-tools..."
    wget https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
    unzip -q commandlinetools-linux-11076708_latest.zip -d latest
fi

# PATH 임시 등록 (sdkmanager 실행용)
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"

# NDK, CMake, 플랫폼 설치
sdkmanager --sdk_root="$ANDROID_HOME" \
    "ndk;$NDK_VERSION" \
    "cmake;3.22.1" \
    "platforms;android-24"

echo "NDK installed at $ANDROID_HOME/ndk/$NDK_VERSION"
