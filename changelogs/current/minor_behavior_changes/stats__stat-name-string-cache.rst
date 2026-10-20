Added an ephemeral, per-flush ``StatNameStringCache`` to memoize string decoding of repeated
``StatName`` tokens and tags during stat sink flushing (e.g., OpenTelemetry, MetricsService, and
Hystrix). This avoids repetitive ``SymbolTable`` mutex acquisitions and varint decoding passes across
metrics sharing common tag keys and values. This optimization is guarded by
``envoy.reloadable_features.enable_stat_name_string_cache`` (enabled by default).
