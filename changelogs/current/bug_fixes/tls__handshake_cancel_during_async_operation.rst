Fixed a bug where Envoy would not detect a downstream connection close
when the TLS handshake was blocked on an asynchronous operation. Only
configurations using certain TLS private key providers, certificate selectors,
or certificate validators were affected.
