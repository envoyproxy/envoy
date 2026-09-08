The SPIFFE certificate validator now supports additional verification of the
upstream peer certificate SAN names via the well-known filter state
``envoy.network.upstream_subject_alt_names``. Both the overridden SAN list and
the configured SAN matchers must match if both are present. This behavior
change can be reverted by setting the runtime guard
``envoy.reloadable_features.spiffe_validator_use_upstream_subject_alt_names``
to ``false``.
