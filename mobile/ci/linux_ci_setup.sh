#!/bin/bash

set -e

# Download and set up ndk 21 after GitHub update
# https://github.com/actions/virtual-environments/issues/5595
ANDROID_HOME=$ANDROID_SDK_ROOT
SDKMANAGER="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager"
$SDKMANAGER --uninstall "ndk-bundle"
echo "y" | $SDKMANAGER "ndk;21.4.7075529"
ln -sfn "${ANDROID_SDK_ROOT}/ndk/21.4.7075529" "${ANDROID_SDK_ROOT}/ndk-bundle"

# Download and set up build-tools 30.0.3, 31.0.0 is missing dx.jar.
$SDKMANAGER --install "build-tools;30.0.3"
echo "ANDROID_NDK_HOME=${ANDROID_HOME}/ndk/21.4.7075529" >> "$GITHUB_ENV"
