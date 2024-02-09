#!/bin/bash

set -e

readonly DEFAULT_VALIDITY_DAYS=${DEFAULT_VALIDITY_DAYS:-730}
HERE=$(cd "$(dirname "$0")" && pwd)
readonly HERE

cd "$HERE" || exit 1
trap cleanup EXIT

cleanup() {
    rm ./*csr
    rm ./*srl
    rm ./crl_*
    rm ./intermediate_crl_*
}

# $1=<CA name> $2=[issuer name]
generate_ca() {
    local extra_args=()
    if [[ -n "$2" ]]; then
        extra_args=(-CA "${2}_cert.pem" -CAkey "${2}_key.pem" -CAcreateserial);
    else
        extra_args=(-signkey "${1}_key.pem");
    fi
    openssl genrsa -out "${1}_key.pem" 2048
    openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" -config "${1}_cert.cfg" -batch -sha256
    openssl x509 -req -days "${DEFAULT_VALIDITY_DAYS}" -in "${1}_cert.csr" -out "${1}_cert.pem" \
            -extensions v3_ca -extfile "${1}_cert.cfg" "${extra_args[@]}"
    generate_info_header "$1"
}

# $1=<certificate name> $2=[key size] $3=[password]
generate_rsa_key() {
    local keysize extra_args=()
    keysize="${2:-2048}"
    if [[ -n "$3" ]]; then
        echo -n "$3" > "${1}_password.txt"
        extra_args=(-aes128 -passout "file:${1}_password.txt")
    fi
    openssl genrsa -out "${1}_key.pem" "${extra_args[@]}" "$keysize"
}

# $1=<certificate name> $2=[curve]
generate_ecdsa_key() {
    local curve
    curve="${2:-secp256r1}"
    openssl ecparam -name "$curve" -genkey -out "${1}_key.pem"
}

# $1=<certificate name>
generate_info_header() {
    local prefix
    prefix="TEST_$(echo "$1" | tr '[:lower:]' '[:upper:]')"
    {
        echo "// NOLINT(namespace-envoy)"
        echo "constexpr char ${prefix}_CERT_256_HASH[] ="
        echo "    \"$(openssl x509 -in "${1}_cert.pem" -outform DER | openssl dgst -sha256 | cut -d" " -f2)\";"
        echo "constexpr char ${prefix}_CERT_1_HASH[] = \"$(openssl x509 -in "${1}_cert.pem" -outform DER | openssl dgst -sha1 | cut -d" " -f2)\";"
        echo "constexpr char ${prefix}_CERT_SPKI[] = \"$(openssl x509 -in "${1}_cert.pem" -noout -pubkey | openssl pkey -pubin -outform DER | openssl dgst -sha256 -binary | openssl enc -base64)\";"
        echo "constexpr char ${prefix}_CERT_SERIAL[] = \"$(openssl x509 -in "${1}_cert.pem" -noout -serial | cut -d"=" -f2 | awk '{print tolower($0)}')\";"
        echo "constexpr char ${prefix}_CERT_NOT_BEFORE[] = \"$(openssl x509 -in "${1}_cert.pem" -noout -startdate | cut -d"=" -f2)\";"
        echo "constexpr char ${prefix}_CERT_NOT_AFTER[] = \"$(openssl x509 -in "${1}_cert.pem" -noout -enddate | cut -d"=" -f2)\";"
    } > "${1}_cert_info.h"
}

# $1=<certificate name> $2=<CA name> $3=[days]
generate_x509_cert() {
    local days extra_args=()
    days="${3:-${DEFAULT_VALIDITY_DAYS}}"
    if [[ -f "${1}_password.txt" ]]; then
        extra_args=(-passin "file:${1}_password.txt")
    fi
    openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" -config "${1}_cert.cfg" -batch -sha256 "${extra_args[@]}"
    openssl x509 -req -days "$days" -in "${1}_cert.csr" -sha256 -CA "${2}_cert.pem" -CAkey \
            "${2}_key.pem" -CAcreateserial -out "${1}_cert.pem" -extensions v3_ca -extfile "${1}_cert.cfg" "${extra_args[@]}"
    generate_info_header "$1"
}

# $1=<certificate name> $2=<CA name> $3=[days]
#
# Generate a certificate without a subject CN. For this to work, the config
# must have an empty [req_distinguished_name] section.
generate_x509_cert_nosubject() {
    local days
    days="${3:-${DEFAULT_VALIDITY_DAYS}}"
    openssl req -new -key "${1}_key.pem" -out "${1}_cert.csr" -config "${1}_cert.cfg" -subj / -batch -sha256
    openssl x509 -req -days "$days" -in "${1}_cert.csr" -sha256 -CA "${2}_cert.pem" -CAkey \
            "${2}_key.pem" -CAcreateserial -out "${1}_cert.pem" -extensions v3_ca -extfile "${1}_cert.cfg"
    generate_info_header "$1"
}

# $1=<certificate name> $2=[certificate file name]
generate_selfsigned_x509_cert() {
    local output_prefix
    output_prefix="${2:-$1}"
    openssl req -new -x509 -days "${DEFAULT_VALIDITY_DAYS}" -key "${1}_key.pem" -out "${output_prefix}_cert.pem" -config "${1}_cert.cfg" -batch -sha256
    generate_info_header "$output_prefix"
}

# $1=<CA name>
# Generates a chain of 3 intermediate certs in test_long_cert_chain
# and a cert signed by this in test_random_cert.pem
generate_cert_chain() {
    local certname
    local ca_name="${1}"
    rm test_long_cert_chain.pem
    touch test_long_cert_chain.pem
    for x in {1..4}; do
        certname="i$x"
        if [[ $x -gt 1 ]]
        then
            ca_name="i$((x - 1))"
        fi
        echo "$x: $certname $ca_name"
        generate_ca "$certname" "$ca_name"
    done
    for x in {1..3}; do
        cat "i${x}_cert.pem" >> test_long_cert_chain.pem
    done
    mv i4_cert.pem test_random_cert.pem

    # These intermediate files are unnecessary.
    for x in {1..4}; do
        rm -f "i${x}_key.pem"
        rm -f "i${x}_cert.pem"
        rm -f "i${x}_cert_info.h"
    done
}

# Generate ca_cert.pem.
generate_ca ca

# Generate intermediate_ca_cert.pem.
generate_ca intermediate_ca ca

# Concatenate intermediate_ca_cert.pem and ca_cert.pem to create valid certificate chain.
cat intermediate_ca_cert.pem ca_cert.pem > intermediate_ca_cert_chain.pem

# Generate a cert-chain with 4 intermediate certs
generate_cert_chain ca

# Generate fake_ca_cert.pem.
generate_ca fake_ca

# Concatenate Fake CA (fake_ca_cert.pem) and Test CA (ca_cert.pem) to create CA file with multiple entries.
cat fake_ca_cert.pem ca_cert.pem > ca_certificates.pem

# Generate no_san_cert.pem.
generate_rsa_key no_san
generate_x509_cert no_san ca

# Concatenate no_san_cert.pem and Test Intermediate CA (intermediate_ca_cert.pem) to create valid certificate chain.
cat no_san_cert.pem intermediate_ca_cert.pem > no_san_chain.pem

# Generate no_san_cn_cert.pem
generate_rsa_key no_san_cn
generate_x509_cert no_san_cn ca

# Generate san_dns_cert.pem.
generate_rsa_key san_dns
generate_x509_cert san_dns ca

# Generate san_dns2_cert.pem (duplicate of san_dns_cert.pem, but with a different private key).
cp -f san_dns_cert.cfg san_dns2_cert.cfg
generate_rsa_key san_dns2
generate_x509_cert san_dns2 ca
rm -f san_dns2_cert.cfg

# Generate san_dns3_cert.pm (signed by intermediate_ca_cert.pem).
cp -f san_dns_cert.cfg san_dns3_cert.cfg
generate_rsa_key san_dns3
generate_x509_cert san_dns3 intermediate_ca
rm -f san_dns3_cert.cfg

# Concatenate san_dns3_cert.pem and Test Intermediate CA (intermediate_ca_cert.pem) to create valid certificate chain.
cat san_dns3_cert.pem intermediate_ca_cert.pem > san_dns3_chain.pem

# Generate san_dns3_certkeychain.p12 with no password.
openssl pkcs12 -export -out san_dns3_certkeychain.p12 -inkey san_dns3_key.pem -in san_dns3_cert.pem -certfile san_dns3_chain.pem -keypbe NONE -certpbe NONE -nomaciter -passout pass:

# Generate san_dns4_cert.pm (signed by intermediate_ca_cert.pem).
cp -f san_dns_cert.cfg san_dns4_cert.cfg
generate_rsa_key san_dns4
generate_x509_cert san_dns4 intermediate_ca
rm -f san_dns4_cert.cfg

# Generate san_wildcard_dns_cert.pem
generate_rsa_key san_wildcard_dns
generate_x509_cert san_wildcard_dns ca

# Generate san_multiple_dns_cert.pem.
generate_rsa_key san_multiple_dns
generate_x509_cert san_multiple_dns ca

# Generate san_multiple_dns_1_cert.pem
generate_rsa_key san_multiple_dns_1
generate_x509_cert san_multiple_dns_1 ca

# Generate san_only_dns_cert.pem.
generate_rsa_key san_only_dns
generate_x509_cert san_only_dns ca

# Generate san_dns_rsa_1_cert.pem
cp san_dns_server1_cert.cfg san_dns_rsa_1_cert.cfg
generate_rsa_key san_dns_rsa_1
generate_x509_cert san_dns_rsa_1 ca
rm -f san_dns_rsa_1_cert.cfg

# Generate san_dns_rsa_2_cert.pem
cp san_dns_server2_cert.cfg san_dns_rsa_2_cert.cfg
generate_rsa_key san_dns_rsa_2
generate_x509_cert san_dns_rsa_2 ca
rm -f san_dns_rsa_2_cert.cfg

# Generate san_dns_ecdsa_1_cert.pem
cp san_dns_server1_cert.cfg san_dns_ecdsa_1_cert.cfg
generate_ecdsa_key san_dns_ecdsa_1
generate_x509_cert san_dns_ecdsa_1 ca
rm -f san_dns_ecdsa_1_cert.cfg

# Generate san_dns_ecdsa_2_cert.pem
cp san_dns_server2_cert.cfg san_dns_ecdsa_2_cert.cfg
generate_ecdsa_key san_dns_ecdsa_2
generate_x509_cert san_dns_ecdsa_2 ca
rm -f san_dns_ecdsa_2_cert.cfg

# Generate san_uri_cert.pem.
generate_rsa_key san_uri
generate_x509_cert san_uri ca

# Generate san_ip_cert.pem.
generate_rsa_key san_ip
generate_x509_cert san_ip ca

# Concatenate san_ip_cert.pem and Test Intermediate CA (intermediate_ca_cert.pem) to create valid certificate chain.
cat san_ip_cert.pem intermediate_ca_cert.pem > san_ip_chain.pem

# Generate certificate with extensions
generate_rsa_key extensions
generate_x509_cert extensions ca

# Generate password_protected_cert.pem.
cp -f san_uri_cert.cfg password_protected_cert.cfg
generate_rsa_key password_protected "" "p4ssw0rd"
generate_x509_cert password_protected ca
rm -f password_protected_cert.cfg

# Generate password_protected_certkey.p12.
openssl pkcs12 -export -out password_protected_certkey.p12 -inkey password_protected_key.pem -in password_protected_cert.pem -passout "file:password_protected_password.txt" -passin "pass:p4ssw0rd"

# Generate selfsigned*_cert.pem.
generate_rsa_key selfsigned
generate_selfsigned_x509_cert selfsigned
generate_selfsigned_x509_cert selfsigned selfsigned2

# Generate selfsigned_rsa_1024.pem
cp -f selfsigned_cert.cfg selfsigned_rsa_1024_cert.cfg
generate_rsa_key selfsigned_rsa_1024 1024
generate_selfsigned_x509_cert selfsigned_rsa_1024
rm -f selfsigned_rsa_1024_cert.cfg

# Generate selfsigned_rsa_1024_certkey.p12 with no password.
openssl pkcs12 -export -out selfsigned_rsa_1024_certkey.p12 -inkey selfsigned_rsa_1024_key.pem -in selfsigned_rsa_1024_cert.pem -keypbe NONE -certpbe NONE -nomaciter -passout pass:

# Generate selfsigned_rsa_3072.pem
cp -f selfsigned_cert.cfg selfsigned_rsa_3072_cert.cfg
generate_rsa_key selfsigned_rsa_3072 3072
generate_selfsigned_x509_cert selfsigned_rsa_3072
rm -f selfsigned_rsa_3072_cert.cfg

# Generate selfsigned_rsa_4096.pem
cp -f selfsigned_cert.cfg selfsigned_rsa_4096_cert.cfg
generate_rsa_key selfsigned_rsa_4096 4096
generate_selfsigned_x509_cert selfsigned_rsa_4096
rm -f selfsigned_rsa_4096_cert.cfg

# Generate selfsigned_ecdsa_p256_cert.pem.
cp -f selfsigned_cert.cfg selfsigned_ecdsa_p256_cert.cfg
generate_ecdsa_key selfsigned_ecdsa_p256
generate_selfsigned_x509_cert selfsigned_ecdsa_p256
generate_selfsigned_x509_cert selfsigned_ecdsa_p256 selfsigned2_ecdsa_p256
rm -f selfsigned_ecdsa_p256_cert.cfg

# Generate selfsigned_ecdsa_p384_cert.pem.
cp -f selfsigned_cert.cfg selfsigned_ecdsa_p384_cert.cfg
generate_ecdsa_key selfsigned_ecdsa_p384 secp384r1
generate_selfsigned_x509_cert selfsigned_ecdsa_p384
rm -f selfsigned_ecdsa_p384_cert.cfg

# Generate selfsigned_ecdsa_p384_certkey.p12 with no password.
openssl pkcs12 -export -out selfsigned_ecdsa_p384_certkey.p12 -inkey selfsigned_ecdsa_p384_key.pem -in selfsigned_ecdsa_p384_cert.pem -keypbe NONE -certpbe NONE -nomaciter -passout pass:

# Generate long_validity_cert.pem as a self-signed, with expiry that exceeds 32bit time_t.
cp -f selfsigned_cert.cfg long_validity_cert.cfg
generate_rsa_key long_validity
generate_x509_cert long_validity ca 18250
rm -f long_validity_cert.cfg

# Generate expired_cert.pem as a self-signed, expired cert (will fail on macOS 10.13+ because of negative days value).
cp -f selfsigned_cert.cfg expired_cert.cfg
generate_rsa_key expired
generate_x509_cert expired ca -365
rm -f expired_cert.cfg

# Generate expired_san_uri_cert.pem as a CA signed, expired cert (will fail on macOS 10.13+ because of negative days value).
cp -f san_uri_cert.cfg expired_san_uri_cert.cfg
generate_rsa_key expired_san_uri
generate_x509_cert expired_san_uri ca -365
rm -f expired_san_uri_cert.cfg

# Initialize information for root CRL process
touch crl_index.txt crl_index.txt.attr
echo 00 > crl_number

# Revoke the certificate and generate a CRL (using root)
openssl ca -revoke san_dns_cert.pem -keyfile ca_key.pem -cert ca_cert.pem -config ca_cert.cfg
openssl ca -gencrl -keyfile ca_key.pem -cert ca_cert.pem -out ca_cert.crl -config ca_cert.cfg
cat ca_cert.pem ca_cert.crl > ca_cert_with_crl.pem

# Initialize information for intermediate CRL process
touch intermediate_crl_index.txt intermediate_crl_index.txt.attr
echo 00 > intermediate_crl_number

# Revoke the certificate and generate a CRL (using intermediate)
openssl ca -revoke san_dns3_cert.pem -keyfile intermediate_ca_key.pem -cert intermediate_ca_cert.pem -config intermediate_ca_cert.cfg
openssl ca -gencrl -keyfile intermediate_ca_key.pem -cert intermediate_ca_cert.pem -out intermediate_ca_cert.crl -config intermediate_ca_cert.cfg
cat ca_cert.crl intermediate_ca_cert.crl > intermediate_ca_cert_chain.crl
cat ca_cert.pem intermediate_ca_cert.pem intermediate_ca_cert.crl > intermediate_ca_cert_chain_with_crl.pem
cat ca_cert.pem intermediate_ca_cert.pem ca_cert.crl intermediate_ca_cert.crl > intermediate_ca_cert_chain_with_crl_chain.pem

# Write session ticket key files
openssl rand 80 > ticket_key_a
openssl rand 80 > ticket_key_b
openssl rand 79 > ticket_key_wrong_len

# Generate a certificate with no subject CN and no altnames.
generate_rsa_key no_subject
generate_x509_cert_nosubject no_subject ca

# Generate unit test certificate
generate_rsa_key unittest
generate_selfsigned_x509_cert unittest

generate_rsa_key keyusage_cert_sign
generate_x509_cert keyusage_cert_sign ca

generate_rsa_key keyusage_crl_sign
generate_x509_cert keyusage_crl_sign ca

generate_rsa_key spiffe_san
generate_x509_cert spiffe_san ca

generate_rsa_key non_spiffe_san
generate_x509_cert non_spiffe_san ca

cp -f spiffe_san_cert.cfg expired_spiffe_san_cert.cfg
generate_rsa_key expired_spiffe_san
generate_x509_cert expired_spiffe_san ca -365
rm -f expired_spiffe_san_cert.cfg

cp -f spiffe_san_cert.cfg spiffe_san_signed_by_intermediate_cert.cfg
generate_rsa_key spiffe_san_signed_by_intermediate
generate_x509_cert spiffe_san_signed_by_intermediate intermediate_ca
rm -f spiffe_san_signed_by_intermediate_cert.cfg
