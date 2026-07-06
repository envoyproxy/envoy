#!/usr/bin/env bash

set -e

# Set up necessary Android SDK and NDK.
ANDROID_HOME=$ANDROID_SDK_ROOT
SDKMANAGER="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager"
"${SDKMANAGER}" --install "platform-tools" "platforms;android-30"
"${SDKMANAGER}" --uninstall "ndk-bundle"
"${SDKMANAGER}" --install "ndk;29.0.14206865"
"${SDKMANAGER}" --install "build-tools;35.0.0"
echo "ANDROID_NDK_HOME=${ANDROID_HOME}/ndk/29.0.14206865" >> "$GITHUB_ENV"
