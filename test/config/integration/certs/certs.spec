# Test certificate fixtures for test/config/integration.
#
# Consumed by @envoy_toolshed//certs:gen (see
# https://github.com/envoyproxy/toolshed/blob/main/bazel/certs/README.md).
# Note that this directory's naming convention has no underscore before "cert".

# --------------------------------------------------------------------------
# Certificate authorities.
# --------------------------------------------------------------------------

[cert ca]
key = cakey.pem
cfg = cacert.cfg
issuer = self
out = cacert.pem
info_header = cacert_info.h

[cert intermediate_ca]
key = intermediate_cakey.pem
cfg = intermediate_cacert.cfg
issuer = ca
out = intermediate_cacert.pem
info_header = intermediate_cacert_info.h

[cert intermediate_ca_2]
key = intermediate_ca_2key.pem
cfg = intermediate_ca_2cert.cfg
issuer = intermediate_ca
out = intermediate_ca_2cert.pem
info_header = intermediate_ca_2cert_info.h

[concat intermediate_ca_cert_chain.pem]
parts = ca, intermediate_ca, intermediate_ca_2

[concat intermediate_partial_ca_cert_chain.pem]
parts = intermediate_ca, intermediate_ca_2

[cert upstreamca]
key = upstreamcakey.pem
cfg = upstreamcacert.cfg
issuer = self
out = upstreamcacert.pem
info_header = upstreamcacert_info.h

# --------------------------------------------------------------------------
# Server certificates.
# --------------------------------------------------------------------------

[cert server]
key = serverkey.pem
cfg = servercert.cfg
issuer = ca
out = servercert.pem
hash_header = servercert_hash.h
info_header = servercert_info.h

# Carries a large SAN list so that the certificate is unusually big.
[cert long_server]
key = long_serverkey.pem
cfg = long_servercert.cfg
issuer = ca
out = long_servercert.pem
hash_header = long_servercert_hash.h
info_header = long_servercert_info.h

[cert server2]
key = server2key.pem
cfg = server2cert.cfg
issuer = ca
out = server2cert.pem
hash_header = server2cert_hash.h
info_header = server2cert_info.h

[cert server_ecdsa]
key = server_ecdsakey.pem
cfg = servercert.cfg
issuer = ca
out = server_ecdsacert.pem
hash_header = server_ecdsacert_hash.h

[cert server_ecdsa_p384]
key = server_ecdsa_p384key.pem
cfg = servercert.cfg
issuer = ca
out = server_ecdsa_p384cert.pem
hash_header = server_ecdsa_p384cert_hash.h

[cert server_ecdsa_p521]
key = server_ecdsa_p521key.pem
cfg = servercert.cfg
issuer = ca
out = server_ecdsa_p521cert.pem
hash_header = server_ecdsa_p521cert_hash.h

# --------------------------------------------------------------------------
# Client certificates.
# --------------------------------------------------------------------------

[cert client]
key = clientkey.pem
cfg = clientcert.cfg
issuer = ca
out = clientcert.pem
hash_header = clientcert_hash.h

[cert client2]
key = client2key.pem
cfg = client2cert.cfg
issuer = intermediate_ca_2
out = client2cert.pem
hash_header = client2cert_hash.h

[concat client2_chain.pem]
parts = client2, intermediate_ca_2, intermediate_ca, ca

[cert client_ecdsa]
key = client_ecdsakey.pem
cfg = clientcert.cfg
issuer = ca
out = client_ecdsacert.pem
hash_header = client_ecdsacert_hash.h

# --------------------------------------------------------------------------
# Upstream certificates.
# --------------------------------------------------------------------------

[cert upstream]
key = upstreamkey.pem
cfg = upstreamcert.cfg
issuer = upstreamca
out = upstreamcert.pem
hash_header = upstreamcert_hash.h

[cert upstreamlocalhost]
key = upstreamlocalhostkey.pem
cfg = upstreamlocalhostcert.cfg
issuer = upstreamca
out = upstreamlocalhostcert.pem
hash_header = upstreamlocalhostcert_hash.h

# The trailing underscore is deliberate; it is part of the existing file and
# constant names (`expired_cert_hash.h`, `TEST_EXPIRED__CERT_HASH`).
[cert expired_]
key = expired_key.pem
cfg = expired_cert.cfg
issuer = ca
validity = expired
out = expired_cert.pem
hash_header = expired_cert_hash.h
info_header = expired_cert_info.h

# --------------------------------------------------------------------------
# OCSP responses. No CA index was ever populated, so every response reports
# `unknown` for the certificate in question.
# --------------------------------------------------------------------------

[ocsp server_ocsp_resp.der]
cert = server
issuer = ca
responder = ca
status = unknown
next_update_days = 730

[ocsp long_server_ocsp_resp.der]
cert = long_server
issuer = ca
responder = ca
status = unknown
next_update_days = 730

[ocsp server_ecdsa_ocsp_resp.der]
cert = server_ecdsa
issuer = ca
responder = ca
status = unknown
next_update_days = 730

[ocsp server_ecdsa_p384_ocsp_resp.der]
cert = server_ecdsa_p384
issuer = ca
responder = ca
status = unknown
next_update_days = 730

[ocsp server_ecdsa_p521_ocsp_resp.der]
cert = server_ecdsa_p521
issuer = ca
responder = ca
status = unknown
next_update_days = 730
