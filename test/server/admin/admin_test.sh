#!/bin/bash
#
# This test is semi-automatic. It builds envoy-static with
# ENVOY_ADMIN_BROWSER_TEST set, then runs the binary in a mode where
# the binary picks the admin port and writes it to a file. Then
# the script can poll the admin port for /ready. Once live, it runs
# Firefox (could be any browser) on the test HTML page, which will
# run through admin tests, printing out test results.
#
# It then leaves Firefox up, so the developer can examine the results
# and then quit firefox.
#
# These steps can also be perrformed manually rather than via the script.

admin_port_file="/tmp/admin_port.$$"

echo "*** Building..."
bazel build \
      --//source/extensions/wasm_runtime/v8:enabled=false \
      --cxxopt=-DENVOY_ADMIN_BROWSER_TEST \
      source/exe:envoy-static

echo "*** Invoking Envoy..."
./bazel-bin/source/exe/envoy-static \
      -c test/server/admin/admin_test.yaml \
      --admin-address-path "$admin_port_file" &

echo "*** Waiting for the server to go live..."
sleep 1
ready=""
admin_port=$(cat $admin_port_file)
while [ $(curl "$admin_port/ready") != "LIVE" ]; do
  sleep 1
done

echo "*** Please ensure Browser test passes and the stats UI looks good..."
browser="firefox"
echo $browser "$admin_port/test?file=admin_test.html" "$admin_port/stats?format=active"
$browser "$admin_port/test?file=admin_test.html" "$admin_port/stats?format=active"

curl -X POST "$admin_port/quitquitquit"
rm -f "$admin_port_file"
wait
