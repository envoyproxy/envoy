#!/bin/bash

set -e

# Set up necessary Android SDK and NDK.
ANDROID_HOME=$ANDROID_SDK_ROOT
SDKMANAGER="${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager"
$SDKMANAGER --uninstall "ndk-bundle"
echo "y" | $SDKMANAGER "ndk;21.4.7075529"
$SDKMANAGER --install "build-tools;30.0.3"
echo "ANDROID_NDK_HOME=${ANDROID_HOME}/ndk/21.4.7075529" >> "$GITHUB_ENV"
