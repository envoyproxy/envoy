# OCSP test fixtures for test/common/tls/ocsp.
#
# Consumed by @envoy_toolshed//certs:gen (see
# https://github.com/envoyproxy/toolshed/blob/main/bazel/certs/README.md).

[cert ca]
key = ca_key.pem
cfg = ca_cert.cfg
issuer = self

[cert intermediate_ca]
key = intermediate_ca_key.pem
cfg = intermediate_ca_cert.cfg
issuer = ca

# Leaves were issued by `openssl ca`, which applied no extensions, so these are
# X.509 v1 certificates. The serial numbers match the CA serial file the old
# script seeded with 1000 (hex).
[cert good]
key = good_key.pem
cfg = good_cert.cfg
issuer = ca
serial = 1000

[cert revoked]
key = revoked_key.pem
cfg = revoked_cert.cfg
issuer = ca
serial = 1001
section = must_staple

[cert ecdsa]
key = ecdsa_key.pem
cfg = ecdsa_cert.cfg
issuer = ca
serial = 1002

# --------------------------------------------------------------------------
# OCSP responses.
# --------------------------------------------------------------------------

[ocsp good_ocsp_resp.der]
cert = good
issuer = ca
responder = ca
status = good
next_update_days = 730
info_header = good_ocsp_resp_info.h
info_header_prefix = good_ocsp_resp

# Identical to good_ocsp_resp.der except that the responder is identified by
# the SHA-1 hash of its public key rather than by name.
[ocsp responder_key_hash_ocsp_resp.der]
cert = good
issuer = ca
responder = ca
responder_id = key
status = good

[ocsp revoked_ocsp_resp.der]
cert = revoked
issuer = ca
responder = ca
status = revoked

# `good_cert.pem` presented to a responder that knows nothing about it.
[ocsp unknown_ocsp_resp.der]
cert = good
issuer = intermediate_ca
responder = intermediate_ca
status = unknown

[ocsp ecdsa_ocsp_resp.der]
cert = ecdsa
issuer = ca
responder = ca
status = good

# Two SingleResponse entries; Envoy rejects responses covering more than one
# certificate.
[ocsp multiple_cert_ocsp_resp.der]
cert = good
issuer = ca
status = good
cert = revoked
issuer = ca
status = revoked
responder = ca
