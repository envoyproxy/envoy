#!/usr/bin/env bash

set -e

if [[ -z "${ANDROID_HOME}" ]]; then
  echo "ANDROID_HOME environment variable must be set."
  exit 1
fi

echo "y" | "${ANDROID_HOME}/cmdline-tools/latest/bin/sdkmanager" --install 'system-images;android-30;google_apis;x86_64' --channel=3
echo "no" | "${ANDROID_HOME}/cmdline-tools/latest/bin/avdmanager" create avd -n test_android_emulator -k 'system-images;android-30;google_apis;x86_64' --device pixel_4 --force
"${ANDROID_HOME}/emulator/emulator" -avd test_android_emulator
