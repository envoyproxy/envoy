tls: fixed enforcement of ``require_client_certificate`` when no trusted CA is configured. Client certificate presentation is now
  required independently of CA-list configuration. With explicit ``ACCEPT_UNTRUSTED``, presented self-signed certificates remain
  accepted. Without a validation context, requiring a certificate also enables normal peer verification, so untrusted certificates
  can be rejected. Optional client certificate presentation is unchanged.
