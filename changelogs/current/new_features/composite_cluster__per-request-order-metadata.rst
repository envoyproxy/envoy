Added :ref:`order_metadata_key
<envoy_v3_api_field_extensions.clusters.composite.v3.ClusterConfig.order_metadata_key>` to the
composite cluster, allowing the sub-cluster order for a single request to be supplied by a filter
as a list of strings in request dynamic metadata. Names that are not present in :ref:`clusters
<envoy_v3_api_field_extensions.clusters.composite.v3.ClusterConfig.clusters>` are ignored, so the
configured list remains the allowlist, and absent or malformed metadata falls back to the
configured order without failing the request.
