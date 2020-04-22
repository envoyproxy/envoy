.. _arch_overview_statistics:

Statistics
==========

One of the primary goals of Envoy is to make the network understandable. Envoy emits a large number
of statistics depending on how it is configured. Generally the statistics fall into three categories:

* **Downstream**: Downstream statistics relate to incoming connections/requests. They are emitted by
  listeners, the HTTP connection manager, the TCP proxy filter, etc.
* **Upstream**: Upstream statistics relate to outgoing connections/requests. They are emitted by
  connection pools, the router filter, the TCP proxy filter, etc.
* **Server**: Server statistics describe how the Envoy server instance is working. Statistics like
  server uptime or amount of allocated memory are categorized here.

A single proxy scenario typically involves both downstream and upstream statistics. The two types
can be used to get a detailed picture of that particular network hop. Statistics from the entire
mesh give a very detailed picture of each hop and overall network health. The statistics emitted are
documented in detail in the operations guide.

As of the v2 API, Envoy has the ability to support custom, pluggable sinks. :ref:`A
few standard sink implementations<envoy_api_msg_config.metrics.v2.StatsSink>` are included in Envoy.
Some sinks also support emitting statistics with tags/dimensions.

Within Envoy and throughout the documentation, statistics are identified by a canonical string
representation. The dynamic portions of these strings are stripped to become tags. Users can
configure this behavior via :ref:`the Tag Specifier configuration <envoy_api_msg_config.metrics.v2.TagSpecifier>`.

Envoy emits three types of values as statistics:

* **Counters**: Unsigned integers that only increase and never decrease. E.g., total requests.
* **Gauges**: Unsigned integers that both increase and decrease. E.g., currently active requests.
* **Histograms**: Unsigned integers that are part of a stream of values that are then aggregated by
  the collector to ultimately yield summarized percentile values. E.g., upstream request time.

Internally, counters and gauges are batched and periodically flushed to improve performance.
Histograms are written as they are received. Note: what were previously referred to as timers have
become histograms as the only difference between the two representations was the units.

* :ref:`v2 API reference <envoy_api_field_config.bootstrap.v2.Bootstrap.stats_sinks>`.
