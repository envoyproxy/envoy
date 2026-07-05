client_side_weighted_round_robin: implemented out-of-band ORCA load reporting via
server-streaming gRPC (``xds.service.orca.v3.OpenRcaService.StreamCoreMetrics``) when
:ref:`enable_oob_load_report
<envoy_v3_api_field_extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin.enable_oob_load_report>`
is true. Cluster-scoped stats are emitted under the ``lb_orca_oob.`` prefix.
