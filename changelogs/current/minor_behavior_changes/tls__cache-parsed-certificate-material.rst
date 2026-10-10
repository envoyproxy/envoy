Parsed TLS certificate chains and private keys are now cached and shared across TLS contexts that
reference identical PEM material, so each distinct certificate and key is parsed once instead of once
per context. Password-protected private keys are not cached. This change can be reverted by setting
the runtime guard ``envoy.reloadable_features.cache_parsed_tls_certificates`` to ``false``.
