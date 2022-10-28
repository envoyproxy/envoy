#!/bin/bash -e

export NAME=tls-sni
export MANUAL=true
export PORT_PROXY="${TLS_SNI_PORT_PROXY:-12020}"
export PORT_PROXY_CLIENT="${TLS_SNI_PORT_PROXY_CLIENT:-12021}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# TODO(phlax): remove openssl bug workaround when openssl/ubuntu are updated
#    see #15555 for more info
touch ~/.rnd

create_self_signed_certs () {
    local domain="$1"
    openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
            -subj "/C=US/ST=CA/O=MyExample, Inc./CN=${domain}.example.com" \
            -keyout "certs/${domain}.key.pem" \
            -out "certs/${domain}.crt.pem"
}

mkdir -p certs

run_log "Create certificates for each of the services"
create_self_signed_certs domain1
create_self_signed_certs domain2

bring_up_example

run_log "Query domain1 with curl and tls/sni"
curl -sk --resolve "domain1.example.com:${PORT_PROXY}:127.0.0.1" \
     "https://domain1.example.com:${PORT_PROXY}" \
    | jq '.os.hostname' | grep http-upstream1

run_log "Query domain2 with curl and tls/sni"
curl -sk --resolve "domain2.example.com:${PORT_PROXY}:127.0.0.1" \
     "https://domain2.example.com:${PORT_PROXY}" \
    | jq '.os.hostname' | grep http-upstream2

run_log "Query domain3 with curl and tls/sni"
curl -sk --resolve "domain3.example.com:${PORT_PROXY}:127.0.0.1" \
     "https://domain3.example.com:${PORT_PROXY}" \
    | jq '.os.hostname' | grep https-upstream3

run_log "Query domain1 via Envoy sni client"
curl -s "http://localhost:${PORT_PROXY_CLIENT}/domain1" \
    | jq '.os.hostname' | grep http-upstream1

run_log "Query domain2 via Envoy sni client"
curl -s "http://localhost:${PORT_PROXY_CLIENT}/domain2" \
    | jq '.os.hostname' | grep http-upstream2

run_log "Query domain3 via Envoy sni client"
curl -s "http://localhost:${PORT_PROXY_CLIENT}/domain3" \
    | jq '.os.hostname' | grep https-upstream3
