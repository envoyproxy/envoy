The statsd, DogStatsD and Graphite statsd sinks now scale histogram samples to milliseconds according to
the histogram's unit before reporting them as timers: samples of histograms recording microseconds are
divided by 1000, while histograms recording milliseconds or without a declared unit are reported unchanged
as before. Previously every sample was reported unchanged with an ``ms`` suffix regardless of its unit.
This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.statsd_scale_histogram_units`` to ``false``.
