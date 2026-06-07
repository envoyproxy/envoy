Added a new :ref:`WASM stats filter <config_stat_sinks_wasm_filter>` contrib extension
(``envoy.stat_sinks.wasm_filter``) that acts as programmable middleware between
the metrics snapshot and any inner stats sink. A user-supplied WASM plugin can:
filter metrics by index, inject global tags from node metadata
(``stats_filter_set_global_tags``), rename metrics (``stats_filter_set_name_overrides``),
inject synthetic counters/gauges (``stats_filter_inject_metrics``), and filter
histograms (``stats_filter_get_histograms``). This enables moving centralized
metric processing logic (tag enrichment, name rewriting, custom metric injection)
into the proxy itself. Configured via
:ref:`WasmFilterStatsSinkConfig <envoy_v3_api_msg_extensions.stat_sinks.wasm_filter.v3.WasmFilterStatsSinkConfig>`.
