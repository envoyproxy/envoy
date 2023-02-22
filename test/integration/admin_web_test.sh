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
# These steps can also be performed manually rather than via the script.

admin_port_file="/tmp/admin_port.$$"

ENVOY_BINARY="bazel-bin/test/integration/admin_test_server"
if [ -e "$ENVOY_BINARY" ]; then
  echo "*** Re-using binary..."
  ls -l "$ENVOY_BINARY"
else
  echo "*** Building..."
  bazel build test/integration:admin_test_server
fi


echo "*** Invoking Envoy..."
$ENVOY_BINARY \
      -c test/integration/admin_web_test.yaml \
      --admin-address-path "$admin_port_file" &

echo "*** Waiting for the server to go live..."
sleep 1
admin_port=$(cat $admin_port_file)
while [ "$(curl $admin_port/ready)" != "LIVE" ]; do
  sleep 1
done

# TODO(jmarantz): at some point it might be worth considering using Selenium
# or other tools to fully automate the testing of the admin UI.
echo "*** Please ensure Browser test passes and the stats UI looks good..."
browser="firefox"
test_url="$admin_port/test?file=admin_web_test.html"
active_stats_url="$admin_port/stats?format=active"
echo $browser "$test_url" "$active_stats_url"
$browser "$test_url" "$active_stats_url"

curl -X POST "$admin_port/quitquitquit"
rm -f "$admin_port_file"
wait
