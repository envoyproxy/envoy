Added :ref:`oob_reporting_config
<envoy_v3_api_field_extensions.load_balancing_policies.client_side_weighted_round_robin.v3.ClientSideWeightedRoundRobin.oob_reporting_config>`
to the ``client_side_weighted_round_robin`` load balancing policy. It configures the ORCA
out-of-band reporting connection: reporting period, an alternative port (e.g. a reporting
sidecar), the ``:authority`` header, and transport socket selection via
``transport_socket_match_criteria``.
