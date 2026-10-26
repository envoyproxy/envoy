# What are the identities, certificates and keys
There are 8 identities:
- **CA**: Certificate Authority for all fixtures in this directory. It has the
  self-signed certificate *ca_cert.pem*. *ca_key.pem* is its private key.
- **Intermediate CA**: Intermediate Certificate Authority, signed by the **CA**.
  It has the certificate *intermediate_ca_cert.pem". *intermediate_ca_key.pem*
  is its private key.
- **Good** It has the certificate *good_cert.pem*, signed by the **CA**. A
  "good" OCSP response is included in *good_ocsp_resp.der*.
- **Responder Key Hash** An OCSP response for the **Good** cert with responder
  key hash replacing the name is included in *responder_key_hash_ocsp_resp.der*.
- **Revoked** It has the revoked certificate *revoked_key.pem*, signed by the
  **CA**. A corresponding revoked OCSP response is included in
  *revoked_ocsp_resp.der*.
- **Unknown** An unknown status OCSP response is generated in
  *unknown_ocsp_resp.der* as the **Good** certificate is signed by **CA** not
  **Intermediate CA**.
- **ECDSA** A cert (*ecdsa_cert.pem*) signed by **CA** with ECDSA key
  (*ecdsa_key.pem*) and OCSP response (*ecdsa_ocsp_resp.der*).
- **Multiple Cert OCSP Response** A multi-cert OCSP response is generated with
  **CA** as the signer for the **Good** and **Revoked** certs in
  *multiple_cert_ocsp_resp.der*.

# How to generate and update certificates
The certificates and OCSP responses in this directory are generated at build
time by
[`@envoy_toolshed//certs:gen`](https://github.com/envoyproxy/toolshed/blob/main/bazel/certs/README.md)
from
[certs.spec](certs.spec). Only the private keys and the `*.cfg` OpenSSL configs
are checked in.

```console
$ bazel build //test/common/tls/ocsp/test_data:certs
$ ls bazel-bin/test/common/tls/ocsp/test_data/
```

The OCSP request DER files and the human-readable response dumps are no longer
produced. Nothing read the requests, and the timestamps that were scraped out
of the old dump are now exposed as constants in the generated
`good_ocsp_resp_info.h`.
`//test/common/tls/ocsp:generated_fixtures_test` asserts that the generated
responses parse with the expected status and expiry.
