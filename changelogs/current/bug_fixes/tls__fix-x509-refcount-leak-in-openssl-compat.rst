Fixed a memory leak in the OpenSSL compatibility layer where ``SSL_get0_peer_certificates()``
called ``SSL_get_peer_certificate()`` without freeing the returned reference. Each call leaked
one ``X509`` refcount, preventing the certificate and its sub-allocations from being freed when
the connection closed, causing unbounded memory growth in certain deployments.
