Implemented the previously-unenforced :ref:`tap_enabled <envoy_v3_api_field_config.tap.v3.TapConfig.tap_enabled>`
runtime fractional percent sampling in the HTTP tap filter (per request) and the tap transport
socket (per connection). The configured fraction of requests/connections is admitted to the
existing match predicate; the remainder is short-circuited before any match state is allocated and
increments the new ``rq_sampled_out`` (HTTP) or ``cx_sampled_out`` (transport socket) stat.
Configurations that already set ``tap_enabled`` (previously accepted and ignored) will begin
sampling on upgrade; this enforcement can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.tap_honor_tap_enabled`` to false.
