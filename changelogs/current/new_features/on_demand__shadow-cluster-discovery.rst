Added :ref:`shadow_cluster_discovery
<envoy_v3_api_field_extensions.filters.http.on_demand.v3.OnDemandCds.shadow_cluster_discovery>` to the
:ref:`on demand filter <config_http_filters_on_demand>`, to discover the clusters that a request is mirrored to
with on-demand CDS. The discovery is best-effort and never delays the request: mirrored requests to a cluster that
is not available yet are dropped, as before, until it has been discovered.
