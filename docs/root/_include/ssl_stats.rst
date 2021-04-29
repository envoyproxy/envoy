.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   connection_error, Counter, Total TLS connection errors not including failed certificate verifications
   handshake, Counter, Total successful TLS connection handshakes
   session_reused, Counter, Total successful TLS session resumptions
   no_certificate, Counter, Total successful TLS connections with no client certificate
   fail_verify_no_cert, Counter, Total TLS connections that failed because of missing client certificate
   fail_verify_error, Counter, Total TLS connections that failed CA verification
   fail_verify_san, Counter, Total TLS connections that failed SAN verification
   fail_verify_cert_hash, Counter, Total TLS connections that failed certificate pinning verification
   ocsp_staple_failed, Counter, Total TLS connections that failed compliance with the OCSP policy
   ocsp_staple_omitted, Counter, Total TLS connections that succeeded without stapling an OCSP response
   ocsp_staple_responses, Counter, Total TLS connections where a valid OCSP response was available (irrespective of whether the client requested stapling)
   ocsp_staple_requests, Counter, Total TLS connections where the client requested an OCSP staple
   ciphers.<cipher>, Counter, Total successful TLS connections that used cipher <cipher>
   curves.<curve>, Counter, Total successful TLS connections that used ECDHE curve <curve>
   sigalgs.<sigalg>, Counter, Total successful TLS connections that used signature algorithm <sigalg>
   versions.<version>, Counter, Total successful TLS connections that used protocol version <version>
