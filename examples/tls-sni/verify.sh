#!/bin/bash -e

export NAME=tls-sni
export MANUAL=true

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


create_self_signed_certs () {
    local domain="$1"
    openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
	    -subj "/C=US/ST=CA/O=MyExample, Inc./CN=${domain}.example.com" \
	    -keyout "certs/${domain}.key" \
	    -out "certs/${domain}.crt"
}

mkdir -p certs

create_self_signed_certs domain1
create_self_signed_certs domain2
create_self_signed_certs domain3

bring_up_example
