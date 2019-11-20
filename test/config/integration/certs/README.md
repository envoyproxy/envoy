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

# How to update certificates
**certs.sh** has the commands to generate all files. Running certs.sh directly
will cause all files to be regenerated. So if you want to regenerate a
particular file, please copy the corresponding commands from certs.sh and
execute them in command line.
