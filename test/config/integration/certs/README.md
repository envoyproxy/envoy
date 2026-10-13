# What are the identities, certificates and keys
There are 5 identities:
- **CA**: Certificate Authority for **Client** and **Server**. It has the
  self-signed certificate *cacert.pem*. *cakey.pem* is its private key.
- **Client**: It has the certificate *clientcert.pem*, signed by the **CA**.
  *clientkey.pem* is its private key.
- **Server**: It has the certificate *servercert.pem*, which is signed by the
  **CA** using the config *servercert.cfg*. *serverkey.pem* is its private key.
- **Upstream CA**: Certificate Authority for **Upstream**. It has the self-signed
  certificate *upstreamcacert.pem*. *upstreamcakey.pem* is its private key.
- **Upstream**: It has the certificate *upstreamcert.pem*, which is signed by
  the **Upstream CA** using the config *upstreamcert.cfg*. *upstreamkey.pem* is
  its private key.
- **Upstream localhost**: It has the certificate *upstreamlocalhostcert.pem*, which is signed by
  the **Upstream CA** using the config *upstreamlocalhostcert.cfg*. *upstreamlocalhostkey.pem* is
  its private key. The different between this certificate and **Upstream** is that this certifcate
  has a SAN for "localhost".

# How to generate and update certificates
The certificates, chains, OCSP responses, `*cert_hash.h` and `*cert_info.h`
headers in this directory are generated at build time by
[`@envoy_toolshed//certs:gen`](https://github.com/envoyproxy/toolshed/blob/main/bazel/certs/README.md)
from
[certs.spec](certs.spec). Only the private keys and the `*.cfg` OpenSSL configs
are checked in.

```console
$ bazel build //test/config/integration/certs:certs
$ ls bazel-bin/test/config/integration/certs/
```

Generating the expired certificate no longer needs `docker run ... faketime`;
the generator writes the notBefore/notAfter fields directly.

`pqc_cacert.pem`, `google_root_certs.pem` and `san_nul_servercert.pem` (see
`generate_nul_cert.py`) are not generated and are checked in as-is.
