tls: fixed enforcement of ``require_client_certificate`` when no trusted CA is configured. Client certificate presentation is now
  required independently of CA-list configuration. With explicit ``ACCEPT_UNTRUSTED``, presented self-signed certificates remain
  accepted. Without a validation context, requiring a certificate also enables normal peer verification, so untrusted certificates
  can be rejected. Optional client certificate presentation is unchanged.
  This change can be temporarily reverted by setting runtime guard
  ``envoy.reloadable_features.require_client_certificate_without_ca`` to ``false``. The guard is evaluated when TLS contexts are
  created; reload affected TLS contexts or restart Envoy after changing it. Existing TLS contexts and connections are unchanged.
