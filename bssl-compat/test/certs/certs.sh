#!/bin/bash

set -e

generate_ca() { # $1=<CA name> $2=[issuer name]
  local extra_args=()
  if [[ -n "$2" ]]; then
      extra_args=(-CA "${2}_cert.pem" -CAkey "${2}_key.pem" -CAcreateserial);
  else
      extra_args=(-signkey "${1}_key.pem");
  fi
  openssl genrsa -out "${1}_key.pem" 2048
  openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" -config "${1}_cert.cfg" -batch -sha256
  openssl x509 -req -days 730 -in "${1}_cert.csr" -out "${1}_cert.pem" -extensions v3_ca -extfile "${1}_cert.cfg" "${extra_args[@]}"
}

generate_rsa_key() { # $1=<certificate name>
  openssl genrsa -out "${1}_key.pem" 2048
}

generate_x509_cert() { # $1=<certificate name> $2=<CA name> $3=[days]
  local days="${3:-730}"
  openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" -config "${1}_cert.cfg" -batch -sha256
  openssl x509 -req -days "${days}" -in "${1}_cert.csr" -sha256 -CA "${2}_cert.pem" -CAkey "${2}_key.pem" -CAcreateserial -out "${1}_cert.pem" -extensions v3_ca -extfile "${1}_cert.cfg"
}


cd "$(dirname "$0")"

generate_ca root_ca
generate_ca intermediate_ca_1 root_ca
generate_ca intermediate_ca_2 intermediate_ca_1

generate_rsa_key server_1
generate_x509_cert server_1 root_ca

generate_rsa_key server_2
generate_x509_cert server_2 intermediate_ca_2

generate_rsa_key client_1
generate_x509_cert client_1 root_ca

generate_rsa_key client_2
generate_x509_cert client_2 intermediate_ca_2

cat server_1_cert.pem root_ca_cert.pem > server_1_cert_chain.pem
cat client_1_cert.pem root_ca_cert.pem > client_1_cert_chain.pem
cat server_2_cert.pem intermediate_ca_2_cert.pem intermediate_ca_1_cert.pem root_ca_cert.pem > server_2_cert_chain.pem
cat client_2_cert.pem intermediate_ca_2_cert.pem intermediate_ca_1_cert.pem root_ca_cert.pem > client_2_cert_chain.pem

for PEM in $(ls *.pem)
do
  echo -n "static const char $(echo $PEM | sed 's/\./_/g')_str[] = R\"\"\"(" > $PEM.h
  cat $PEM >> $PEM.h
  echo ")\"\"\";" >> $PEM.h
done

rm *.csr *.srl
