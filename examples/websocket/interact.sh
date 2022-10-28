#!/bin/bash -e

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
spawn websocat $insecure $protocol://localhost:$port
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

interact_ws "$@"
