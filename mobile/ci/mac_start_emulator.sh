#!/bin/bash

set -e

echo "y" | $ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager --install 'system-images;android-29;google_apis;x86' --channel=3
echo "no" | $ANDROID_HOME/tools/bin/avdmanager create avd -n test_android_emulator -k 'system-images;android-29;google_apis;x86' --force
ls $ANDROID_HOME/tools/bin/
nohup $ANDROID_HOME/emulator/emulator -partition-size 1024 -avd test_android_emulator -no-snapshot > /dev/null 2>&1 & $ANDROID_HOME/platform-tools/adb wait-for-device shell 'while [[ -z $(getprop sys.boot_completed | tr -d '\r') ]]; do sleep 1; done; input keyevent 82'
