#!/bin/bash

set -e

./bazelw version

sdk_install_target="/github/home/.android"

mkdir "$sdk_install_target"
pushd "$sdk_install_target"
if [ ! -d ./sdk/cmdline-tools/latest ]; then
    mkdir -p sdk/
    cmdline_file="commandlinetools-linux-7583922_latest.zip"
    curl -OL "https://dl.google.com/android/repository/$cmdline_file"
    unzip "$cmdline_file"
    mkdir -p sdk/cmdline-tools/latest
    mv cmdline-tools/* sdk/cmdline-tools/latest
fi

ANDROID_HOME="$(realpath "$sdk_install_target/sdk")"
SDKMANAGER=$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager

echo "y" | $SDKMANAGER --install "ndk;21.4.7075529"
$SDKMANAGER --install "platforms;android-30"
$SDKMANAGER --install "build-tools;30.0.2"

{
    echo "ANDROID_HOME=${ANDROID_HOME}"
    echo "ANDROID_SDK_ROOT=${ANDROID_HOME}"
    echo "ANDROID_NDK_HOME=${ANDROID_HOME}/ndk/21.4.7075529"
}  >> "$GITHUB_ENV"
