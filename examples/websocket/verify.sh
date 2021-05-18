#!/bin/bash -e
#
# Requirements: expect
#

export NAME=tls
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# TODO(phlax): remove openssl bug workaround when openssl/ubuntu are updated
#    see #15555 for more info
touch ~/.rnd

interact_ws () {
    local port="$1" \
          protocol="$2" \
          backend="$3" \
          insecure=""
    if [ "$protocol" == "wss" ]; then
        insecure="--insecure"
    fi
    expect <<EOF
set timeout 1
spawn docker run --rm -ti --network=host solsson/websocat $insecure $protocol://127.0.0.1:$port
set ret 1
expect "\n"
send "HELO\n"
expect {
  -ex "\[$backend\] HELO" {
    send "GOODBYE\n"
    expect {
      -ex "\[$backend\] HELO" { set ret 0 }
    }
  }
}
send \x03
exit \$ret
EOF
}

run_log "Generate wss certs"
mkdir -p certs
openssl req -batch -new -x509 -nodes -keyout certs/key.pem -out certs/cert.pem
openssl pkcs12 -export -passout pass: -out certs/output.pkcs12 -inkey certs/key.pem -in certs/cert.pem

bring_up_example

run_log "Interact with web socket ws -> ws"
interact_ws 10000 ws ws

run_log "Interact with web socket wss -> wss"
interact_ws 20000 wss wss

run_log "Interact with web socket wss passthrough"
interact_ws 30000 wss wss
