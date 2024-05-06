#!/usr/bin/env bash

set -euo pipefail

simulator_name="iPhone 14 Pro Max"
simulator_uuid="$(xcrun simctl list | sed -nr "s/.*$simulator_name \(([A-Z0-9\-]{36})\).*/\1/p" | head -n1)"

echo "Booting simulator named '$simulator_name' with uuid '$simulator_uuid'"

open -a "$(xcode-select -p)/Applications/Simulator.app" --args -CurrentDeviceUDID "$simulator_uuid"

attempt=0
max=5
delay=5
while true; do
  # shellcheck disable=SC2015
  (xcrun simctl list | grep "($simulator_uuid) (Booted)") && break || {
    if [[ $attempt -lt $max ]]; then
      ((attempt++))
      echo "Simulator not yet booted. Attempt $attempt/$max. Waiting $delay seconds."
      sleep $delay;
    else
      echo "The simulator did not boot after $attempt attempts."
      exit 1
    fi
  }
done

echo "Simulator booted successfully"
