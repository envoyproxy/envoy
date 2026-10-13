Added ``Nanoseconds`` to the histogram units, so histograms created by extensions such as the Lua filter
(unit ``"nanoseconds"``) and by embedders can record nanosecond samples. Timespans flushing to such a histogram
record the elapsed nanoseconds, and the statsd, DogStatsD and Graphite statsd sinks scale the samples to
milliseconds when ``scale_histogram_units_to_milliseconds`` is enabled. No Envoy histogram uses the new unit,
so existing output is unchanged. Note that the default Prometheus and OpenTelemetry bucket boundaries assume
milliseconds, so nanosecond histograms need explicit :ref:`histogram bucket settings
<envoy_v3_api_field_config.metrics.v3.StatsConfig.histogram_bucket_settings>`.
