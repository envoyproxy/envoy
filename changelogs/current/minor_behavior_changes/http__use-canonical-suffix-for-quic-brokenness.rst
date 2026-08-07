The runtime guard ``envoy.reloadable_features.use_canonical_suffix_for_quic_brokenness`` now defaults
to ``true``. When enabled, the HTTP server properties cache uses configured canonical suffixes to
share QUIC brokenness status across matching origins.
