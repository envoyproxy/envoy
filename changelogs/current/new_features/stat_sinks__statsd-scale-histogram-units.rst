Added :ref:`scale_histogram_units_to_milliseconds
<envoy_v3_api_field_config.metrics.v3.StatsdSink.scale_histogram_units_to_milliseconds>` to the statsd,
:ref:`DogStatsD <envoy_v3_api_field_config.metrics.v3.DogStatsdSink.scale_histogram_units_to_milliseconds>` and
:ref:`Graphite statsd
<envoy_v3_api_field_extensions.stat_sinks.graphite_statsd.v3.GraphiteStatsdSink.scale_histogram_units_to_milliseconds>`
sinks. When enabled, histogram samples are scaled to milliseconds according to the histogram's unit before
being reported as timers: samples of histograms recording microseconds are divided by 1000 and reported as
a fractional millisecond value, while histograms recording milliseconds or without a declared unit are
reported unchanged. By default every sample is still reported unchanged with an ``ms`` suffix regardless of
its unit.
