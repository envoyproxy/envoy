load_balancing: implemented the
:ref:`envoy.load_balancing_policies.load_aware_locality
<envoy_v3_api_msg_extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality>`
locality-picking load balancer. It weights localities by ORCA-derived utilization headroom,
consumes in-band ORCA reporting, and applies at all priority levels. The extension is
work-in-progress and not intended for production use.
