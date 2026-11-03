The SPIFFE certificate validator now rejects a ``trust_domains`` entry whose ``trust_bundle`` parses
but contains no certificate, for example a bundle holding only a CRL. Such a trust domain previously
loaded successfully but had no trust anchor, so every handshake for it failed at verification time
instead. This matches the behavior of the ``trust_bundles`` (SPIFFE bundle map) configuration, which
already rejects a trust domain with no certificate. This change can be reverted by setting the
runtime guard ``envoy.reloadable_features.spiffe_validator_reject_empty_trust_bundle`` to ``false``.
