load_balancing: implemented the
:ref:`envoy.load_balancing_policies.load_aware_locality
<envoy_v3_api_msg_extensions.load_balancing_policies.load_aware_locality.v3.LoadAwareLocality>`
locality-picking load balancer. It weights localities by ORCA-derived utilization headroom
and applies at all priority levels. ORCA data may be consumed in-band, out-of-band (setting
``enable_oob_load_report`` opens a per-host reporting stream, with optional connection
overrides via ``oob_reporting_config``), or both.
