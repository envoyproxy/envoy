# Test certificate fixtures for test/common/tls.
#
# Consumed by @envoy_toolshed//certs:gen (see
# https://github.com/envoyproxy/toolshed/blob/main/bazel/certs/README.md for the
# format). Every file listed as an output here must also appear in the `outs` of
# the `certs` genrule in this directory's BUILD file.
#
# Serial numbers are derived deterministically from the fixture name unless
# pinned with `serial = <hex>`.

# --------------------------------------------------------------------------
# Certificate authorities.
# --------------------------------------------------------------------------

[cert ca]
key = ca_key.pem
cfg = ca_cert.cfg
issuer = self
info_header = ca_cert_info.h

[cert intermediate_ca]
key = intermediate_ca_key.pem
cfg = intermediate_ca_cert.cfg
issuer = ca
info_header = intermediate_ca_cert_info.h

[concat intermediate_ca_cert_chain.pem]
parts = intermediate_ca, ca

[cert fake_ca]
key = fake_ca_key.pem
cfg = fake_ca_cert.cfg
issuer = self
info_header = fake_ca_cert_info.h

[concat ca_certificates.pem]
parts = fake_ca, ca

# --------------------------------------------------------------------------
# A four deep chain of intermediates. i1..i3 are only ever consumed as part of
# test_long_cert_chain.pem; i4 is emitted as test_random_cert.pem.
# --------------------------------------------------------------------------

[cert i1]
key = i1_key.pem
cfg = i1_cert.cfg
issuer = ca
out = none

[cert i2]
key = i2_key.pem
cfg = i2_cert.cfg
issuer = i1
out = none

[cert i3]
key = i3_key.pem
cfg = i3_cert.cfg
issuer = i2
out = none

[cert i4]
key = i4_key.pem
cfg = i4_cert.cfg
issuer = i3
out = test_random_cert.pem

[concat test_long_cert_chain.pem]
parts = i1, i2, i3

# --------------------------------------------------------------------------
# Leaf certificates.
# --------------------------------------------------------------------------

[cert no_san]
key = no_san_key.pem
cfg = no_san_cert.cfg
issuer = ca
info_header = no_san_cert_info.h

[concat no_san_chain.pem]
parts = no_san, intermediate_ca

[cert no_san_cn]
key = no_san_cn_key.pem
cfg = no_san_cn_cert.cfg
issuer = ca
info_header = no_san_cn_cert_info.h

[cert san_dns]
key = san_dns_key.pem
cfg = san_dns_cert.cfg
issuer = ca
info_header = san_dns_cert_info.h

[cert san_dns_cert_with_single_crl_dp]
key = san_dns_cert_with_single_crl_dp_key.pem
cfg = san_dns_cert_with_single_crl_dp_cert.cfg
issuer = ca
info_header = san_dns_cert_with_single_crl_dp_cert_info.h

[cert san_dns_cert_with_multiple_crl_dps]
key = san_dns_cert_with_multiple_crl_dps_key.pem
cfg = san_dns_cert_with_multiple_crl_dps_cert.cfg
issuer = ca
info_header = san_dns_cert_with_multiple_crl_dps_cert_info.h

# Duplicate of san_dns_cert.pem, but with a different private key.
[cert san_dns2]
key = san_dns2_key.pem
cfg = san_dns_cert.cfg
issuer = ca
info_header = san_dns2_cert_info.h

[cert san_dns3]
key = san_dns3_key.pem
cfg = san_dns_cert.cfg
issuer = intermediate_ca
info_header = san_dns3_cert_info.h

[concat san_dns3_chain.pem]
parts = san_dns3, intermediate_ca

# A chain that includes a CA which did not sign the leaf, used to check that
# the correct issuer is still identified.
[concat san_dns3_with_decoy_chain.pem]
parts = san_dns3, ca, intermediate_ca

[p12 san_dns3_certkeychain.p12]
cert = san_dns3
chain = san_dns3, intermediate_ca
password =
encrypt = none

[cert san_dns4]
key = san_dns4_key.pem
cfg = san_dns_cert.cfg
issuer = intermediate_ca
info_header = san_dns4_cert_info.h

[cert san_wildcard_dns]
key = san_wildcard_dns_key.pem
cfg = san_wildcard_dns_cert.cfg
issuer = ca
info_header = san_wildcard_dns_cert_info.h

[cert san_multiple_dns]
key = san_multiple_dns_key.pem
cfg = san_multiple_dns_cert.cfg
issuer = ca
info_header = san_multiple_dns_cert_info.h

[cert san_multiple_dns_1]
key = san_multiple_dns_1_key.pem
cfg = san_multiple_dns_1_cert.cfg
issuer = ca
info_header = san_multiple_dns_1_cert_info.h

[cert san_only_dns]
key = san_only_dns_key.pem
cfg = san_only_dns_cert.cfg
issuer = ca
info_header = san_only_dns_cert_info.h

[cert san_dns_rsa_1]
key = san_dns_rsa_1_key.pem
cfg = san_dns_server1_cert.cfg
issuer = ca
info_header = san_dns_rsa_1_cert_info.h

[cert san_dns_rsa_2]
key = san_dns_rsa_2_key.pem
cfg = san_dns_server2_cert.cfg
issuer = ca
info_header = san_dns_rsa_2_cert_info.h

[cert san_dns_ecdsa_1]
key = san_dns_ecdsa_1_key.pem
cfg = san_dns_server1_cert.cfg
issuer = ca
info_header = san_dns_ecdsa_1_cert_info.h

[cert san_dns_ecdsa_2]
key = san_dns_ecdsa_2_key.pem
cfg = san_dns_server2_cert.cfg
issuer = ca
info_header = san_dns_ecdsa_2_cert_info.h

[cert san_uri]
key = san_uri_key.pem
cfg = san_uri_cert.cfg
issuer = ca
info_header = san_uri_cert_info.h

[cert san_ip]
key = san_ip_key.pem
cfg = san_ip_cert.cfg
issuer = ca
info_header = san_ip_cert_info.h

[concat san_ip_chain.pem]
parts = san_ip, intermediate_ca

[cert san_othername]
key = san_othername_key.pem
cfg = san_othername_cert.cfg
issuer = ca
info_header = san_othername_cert_info.h

[cert san_multiple_othername]
key = san_multiple_othername_key.pem
cfg = san_multiple_othername_cert.cfg
issuer = ca
info_header = san_multiple_othername_cert_info.h

[cert san_multiple_othername_string_type]
key = san_multiple_othername_string_type_key.pem
cfg = san_multiple_othername_string_type_cert.cfg
issuer = ca
info_header = san_multiple_othername_string_type_cert_info.h

[cert san_dns_and_othername]
key = san_dns_and_othername_key.pem
cfg = san_dns_and_othername_cert.cfg
issuer = ca
info_header = san_dns_and_othername_cert_info.h

[cert extensions]
key = extensions_key.pem
cfg = extensions_cert.cfg
issuer = ca
info_header = extensions_cert_info.h

# --------------------------------------------------------------------------
# Password protected key material.
# --------------------------------------------------------------------------

[cert password_protected]
key = password_protected_key.pem
key_password = p4ssw0rd
cfg = san_uri_cert.cfg
issuer = ca
info_header = password_protected_cert_info.h

[p12 password_protected_certkey.p12]
cert = password_protected
password_file = password_protected_password.txt

# --------------------------------------------------------------------------
# Self signed certificates.
# --------------------------------------------------------------------------

[cert selfsigned]
key = selfsigned_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_cert_info.h

# Same key and subject as `selfsigned`, but a distinct certificate.
[cert selfsigned2]
key = selfsigned_key.pem
cfg = selfsigned_cert.cfg
issuer = self
out = selfsigned2_cert.pem
info_header = selfsigned2_cert_info.h

[cert selfsigned_rsa_1024]
key = selfsigned_rsa_1024_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_rsa_1024_cert_info.h

[p12 selfsigned_rsa_1024_certkey.p12]
cert = selfsigned_rsa_1024
password =
encrypt = none

[cert selfsigned_rsa_3072]
key = selfsigned_rsa_3072_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_rsa_3072_cert_info.h

[cert selfsigned_rsa_4096]
key = selfsigned_rsa_4096_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_rsa_4096_cert_info.h

[cert selfsigned_ecdsa_p256]
key = selfsigned_ecdsa_p256_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_ecdsa_p256_cert_info.h

[cert selfsigned2_ecdsa_p256]
key = selfsigned_ecdsa_p256_key.pem
cfg = selfsigned_cert.cfg
issuer = self
out = selfsigned2_ecdsa_p256_cert.pem
info_header = selfsigned2_ecdsa_p256_cert_info.h

[cert selfsigned_ecdsa_p384]
key = selfsigned_ecdsa_p384_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_ecdsa_p384_cert_info.h

[p12 selfsigned_ecdsa_p384_certkey.p12]
cert = selfsigned_ecdsa_p384
password =
encrypt = none

[cert selfsigned_ecdsa_p521]
key = selfsigned_ecdsa_p521_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_ecdsa_p521_cert_info.h

[cert selfsigned_secp224r1]
key = selfsigned_secp224r1_key.pem
cfg = selfsigned_cert.cfg
issuer = self
info_header = selfsigned_secp224r1_cert_info.h

[cert unittest]
key = unittest_key.pem
cfg = unittest_cert.cfg
issuer = self
info_header = unittest_cert_info.h

# --------------------------------------------------------------------------
# Unusual validity periods.
# --------------------------------------------------------------------------

# Expiry that exceeds 32 bit time_t.
[cert long_validity]
key = long_validity_key.pem
cfg = selfsigned_cert.cfg
issuer = ca
validity = long
info_header = long_validity_cert_info.h

[cert expired]
key = expired_key.pem
cfg = selfsigned_cert.cfg
issuer = ca
validity = expired
info_header = expired_cert_info.h

[cert expired_san_uri]
key = expired_san_uri_key.pem
cfg = san_uri_cert.cfg
issuer = ca
validity = expired
info_header = expired_san_uri_cert_info.h

# --------------------------------------------------------------------------
# Certificates without a subject or with restricted key usage.
# --------------------------------------------------------------------------

[cert no_subject]
key = no_subject_key.pem
cfg = no_subject_cert.cfg
issuer = ca
info_header = no_subject_cert_info.h

[cert keyusage_cert_sign]
key = keyusage_cert_sign_key.pem
cfg = keyusage_cert_sign_cert.cfg
issuer = ca
info_header = keyusage_cert_sign_cert_info.h

[cert keyusage_crl_sign]
key = keyusage_crl_sign_key.pem
cfg = keyusage_crl_sign_cert.cfg
issuer = ca
info_header = keyusage_crl_sign_cert_info.h

# --------------------------------------------------------------------------
# SPIFFE.
# --------------------------------------------------------------------------

[cert spiffe_san]
key = spiffe_san_key.pem
cfg = spiffe_san_cert.cfg
issuer = ca
info_header = spiffe_san_cert_info.h

[cert non_spiffe_san]
key = non_spiffe_san_key.pem
cfg = non_spiffe_san_cert.cfg
issuer = ca
info_header = non_spiffe_san_cert_info.h

[cert expired_spiffe_san]
key = expired_spiffe_san_key.pem
cfg = spiffe_san_cert.cfg
issuer = ca
validity = expired
info_header = expired_spiffe_san_cert_info.h

[cert spiffe_san_signed_by_intermediate]
key = spiffe_san_signed_by_intermediate_key.pem
cfg = spiffe_san_cert.cfg
issuer = intermediate_ca
info_header = spiffe_san_signed_by_intermediate_cert_info.h

[trust_bundle trust_bundles.json]
domain = example.com:ca:12035488
domain = lyft.com:ca:12035489

# --------------------------------------------------------------------------
# Revocation lists.
# --------------------------------------------------------------------------

[crl ca_cert.crl]
issuer = ca
revoke = san_dns

[concat ca_cert_with_crl.pem]
parts = ca, ca_cert.crl

[crl intermediate_ca_cert.crl]
issuer = intermediate_ca
revoke = san_dns3

[concat intermediate_ca_cert_chain.crl]
parts = ca_cert.crl, intermediate_ca_cert.crl

[concat intermediate_ca_cert_chain_with_crl.pem]
parts = ca, intermediate_ca, intermediate_ca_cert.crl

[concat intermediate_ca_cert_chain_with_crl_chain.pem]
parts = ca, intermediate_ca, ca_cert.crl, intermediate_ca_cert.crl
