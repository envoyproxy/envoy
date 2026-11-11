Added ``envoy_dynamic_module_callback_stat_sink_snapshot_retain`` and
``envoy_dynamic_module_callback_stat_sink_snapshot_release`` to the dynamic modules stats sink ABI.
A module can keep a flush snapshot readable after the flush hook returns and read it on another
thread, so formatting the stats no longer has to run on the main thread. A retained snapshot holds
a reference on each stat and reports the values copied when it was retained. The Rust SDK exposes
this as ``MetricSnapshot::retain``, which returns a ``Send`` ``RetainedMetricSnapshot`` that
releases the snapshot when it is dropped.
