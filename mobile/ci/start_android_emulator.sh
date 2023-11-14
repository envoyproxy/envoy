#!/bin/bash

set -e

check_emulator_status() {
    while true; do
        if grep -q "Running on a system with less than 6 logical cores. Setting number of virtual cores to 1" nohup.out; then
            echo "=================================================================================="
            echo "ERROR: Starting an emulator on this machine is likely to fail, please run /retest"
            echo "=================================================================================="
            exit 1
        elif grep -q "Boot completed" nohup.out; then
            break
        fi
        sleep 1
    done
}

echo "y" | "${ANDROID_HOME}/cmdline-tools/latest/bin/sdkmanager" --install 'system-images;android-30;google_apis;x86_64' --channel=3
echo "no" | "${ANDROID_HOME}/cmdline-tools/latest/bin/avdmanager" create avd -n test_android_emulator -k 'system-images;android-30;google_apis;x86_64' --device pixel_4 --force
"${ANDROID_HOME}"/emulator/emulator -accel-check
# This is only available on macOS.
if [[ -n $(which system_profiler) ]]; then
    system_profiler SPHardwareDataType
fi

# shellcheck disable=SC2094
nohup "${ANDROID_HOME}/emulator/emulator" -no-window -accel on -gpu swiftshader_indirect -no-snapshot -noaudio -no-boot-anim -avd test_android_emulator > nohup.out 2>&1 | tail -f nohup.out & {
    if [[ "$(uname -s)" == "Darwin" ]]; then
        check_emulator_status
    fi
    # shellcheck disable=SC2016
    "${ANDROID_HOME}/platform-tools/adb" wait-for-device shell 'while [[ -z $(getprop sys.boot_completed | tr -d '\''\r'\'') ]]; do sleep 1; done; input keyevent 82'
}
