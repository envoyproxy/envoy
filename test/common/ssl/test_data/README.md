# What are the identities, certificates and keys
There are 10 identities:
- **CA**: Certificate Authority for **No SAN**, **SAN With URI** and **SAN With
  DNS**. It has the self-signed certificate *ca_cert.pem*. *ca_key.pem* is its
  private key. Additionally, we create a CRL for this CA (*ca_cert.crl*) that
  revokes the certificate *san_dns_cert.pem*.
- **Intermediate CA**: Intermediate Certificate Authority, signed by the **CA**.
  It has the certificate *intermediate_ca_cert.pem". *intermediate_ca_key.pem*
  is its private key.
- **Fake CA**: Fake Certificate Authority used to validate verification logic.
  It has the self-signed certificate *fake_ca_cert.pem"*. *fake_ca_key.pem" is
  its private key.
- **No SAN**: It has the certificate *no_san_cert.pem*, signed by the **CA**.
  The certificate does not have SAN field. *no_san_key.pem* is its private key.
- **SAN With URI**: It has the certificate *san_uri_cert.pem*, which is signed
  by the **CA** using the config *san_uri_cert.cfg*. The certificate has SAN
  field of URI type. *san_uri_key.pem* is its private key.
- **SAN With DNS**: It has the certificate *san_dns_cert.pem*, which is signed
  by the **CA** using the config *san_dns_cert.cfg*. The certificate has SAN
  field of DNS type. *san_dns_key.pem* is its private key. A second certificate
  and key, using the same config, is *san_dns_cert2*. A third certificate and key,
  using the same config, but signed by the **Intermediate CA** is *san_dns_cert3*,
  its certificate chain is *san_dns_chain3.pem*.
- **SAN With Multiple DNS**: Same as *SAN With DNS* except there are multiple
  SANs (including wildcard domain). It has certificate *san_multiple_dns_cert.pem*,
  *san_multiple_dns_key.pem* is its private key.
- **SAN only**: Same as *SAN With DNS* except that the certificate doesn't have the
  CommonName set. It has certificate *san_only_dns_cert.pem*, *san_only_dns_key.pem*
  is its private key.
- **Self-signed**: The self-signed certificate *selfsigned_cert.pem*, using the
  config *selfsigned_cert.cfg*. *selfsigned_key.pem* is its private key.
- **Expired**: A self-signed, expired certificate *expired_cert.pem*,
  using the config *selfsigned_cert.cfg*. *expired_key.pem* is its private
  key.

# How to update certificates
**certs.sh** has the commands to generate all files except the private key
files. Running certs.sh directly will cause the certificate files to be
regenerated. So if you want to regenerate a particular file, please copy the
corresponding commands from certs.sh and execute them in command line.

Note that Mac OS is unable to generate the expired unit test cert starting
with its switch from OpenSSL to LibreSSL in High Sierra (10.13). Specifically,
that version of the openssl command will not accept a non-positive "-days"
parameter.
