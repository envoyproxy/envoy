Added stats sink snapshot getters that expose each metric's tag-extracted name and its tags
(name/value pairs) for counters, gauges, text readouts, and histograms, so a dynamic module can reconstruct
the dimensional metric names Envoy's built-in formatters produce. Available through the Rust SDK
``MetricSnapshot`` tag accessors.
