Added a new dynamic modules stats sink extension (``envoy.stat_sinks.dynamic_modules``) that
delegates metric flushing and histogram observations to a dynamic module loaded via ``dlopen``.
On each flush the module receives a snapshot of the counters, gauges, and text readouts, plus a
callback for every completed histogram sample. Metric names are decoded directly into a
module-provided buffer to avoid intermediate allocations. The Rust and Go SDKs expose this through
a ``StatSink`` interface and a matching stat sink init function. See
:ref:`DynamicModuleStatsSink <envoy_v3_api_msg_extensions.stat_sinks.dynamic_modules.v3.DynamicModuleStatsSink>`
for configuration details.
