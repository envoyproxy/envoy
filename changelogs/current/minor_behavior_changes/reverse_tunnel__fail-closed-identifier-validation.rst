The ``reverse_tunnel`` network filter now fails closed when a configured identifier validation
formatter (``node_id_format``, ``cluster_id_format``, ``tenant_id_format``) renders an empty string
or the absent-value placeholder ``-``. Previously such a render silently skipped the identity
binding and accepted the claimed identifier. A present-but-empty
``x-envoy-reverse-tunnel-tenant-id`` header value is now rejected with a 400 error, matching the
existing missing-header rejection and the empty-identifier guards already applied to node and
cluster ids at socket registration.
