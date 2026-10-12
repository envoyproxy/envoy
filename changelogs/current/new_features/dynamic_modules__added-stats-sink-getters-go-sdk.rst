Extended the dynamic modules Go SDK ``MetricSnapshot`` with histogram getters (``HistogramCount``,
``GetHistogram``, ``HistogramBucketCount``, and ``GetHistogramBucket``) and per-metric tag getters
(tag-extracted name, tag count, and individual tags) for counters, gauges, text readouts, and
histograms, reaching parity with the Rust SDK so Go stats sinks can read histogram values and metric
tags during flush.
