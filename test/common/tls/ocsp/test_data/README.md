# What are the identities, certificates and keys
There are 8 identities:
- **CA**: Certificate Authority for all fixtures in this directory. It has the
  self-signed certificate *ca_cert.pem*. *ca_key.pem* is its private key.
- **Intermediate CA**: Intermediate Certificate Authority, signed by the **CA**.
  It has the certificate *intermediate_ca_cert.pem". *intermediate_ca_key.pem*
  is its private key.
- **Good** It has the certificate *good_cert.pem*, signed by the **CA**. An OCSP
  request is included in *good_ocsp_req.der* and a "good" OCSP response is included in *good_ocsp_resp.der*. OCSP response details are included as
  *good_ocsp_resp_details.txt*.
- **Responder Key Hash** An OCSP request and response pair for the **Good** cert
  with responder key hash replacing the name in *responder_key_hash_ocsp_req.der*
  and *responder_key_hash_ocsp_resp.der*
- **Revoked** It has the revoked certificate *revoked_key.pem*, signed by the
  **CA**. A corresponding OCSP request and revoked response are included in
  *revoked_ocsp_req.der* and *revoked_ocsp_resp.der*.
- **Unknown** An OCSP request and unknown status response is generated in
  *unknown_ocsp_req.der* and *unknown_ocsp_resp.der* as the **Good** certificate
  is signed by **CA** not **Intermediate CA**.
- **ECDSA** A cert (*ecdsa_cert.pem*) signed by **CA** with ECDSA key
  (*ecdsa_key.pem*) and OCSP response (*ecdsa_ocsp_resp.der*).
- **Multiple Cert OCSP Response** A multi-cert OCSP request and response are
  generated with **CA** as the signer for the **Good** and **Revoked** certs in
  *multiple_cert_ocsp_req.der* and *multiple_cert_ocsp_resp.der*.

# How to update certificates
**certs.sh** has the commands to generate all files. Running certs.sh directly
will cause all files to be regenerated. So if you want to regenerate a
particular file, please copy the corresponding commands from certs.sh and
execute them in command line.
