.. _arch_overview_statistics:

Statistics
==========

One of the primary goals of Envoy is to make the network understandable. Envoy emits a large number
of statistics depending on how it is configured. Generally the statistics fall into two categories:

* **Downstream**: Downstream statistics relate to incoming connections/requests. They are emitted by
  listeners, the HTTP connection manager, the TCP proxy filter, etc.
* **Upstream**: Upstream statistics relate to outgoing connections/requests. They are emitted by
  connection pools, the router filter, the TCP proxy filter, etc.

A single proxy scenario typically involves both downstream and upstream statistics. The two types
can be used to get a detailed picture of that particular network hop. Statistics from the entire
mesh give a very detailed picture of each hop and overall network health. The statistics emitted are
documented in detail in the operations guide.

Envoy uses statsd as the statistics output format, though plugging in a different statistics sink
would not be difficult. Both TCP and UDP statsd is supported. Internally, counters and gauges are
batched and periodically flushed to improve performance. Histograms are written as they are
received. Note: what were previously referred to as timers have become histograms as the only
difference between the two representations was the units.

Statistics :ref:`configuration <config_overview>`.
