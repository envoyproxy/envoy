.. _arch_overview_upstream_filters:

Upstream network filters
========================

Upstream clusters provide an ability to inject network level (L3/L4)
:ref:`filters <arch_overview_network_filters>`. The filters apply to the
connection to the upstream hosts, using the same API presented by listeners for
the downstream connections. The write callbacks are invoked for any chunk of
data sent to the upstream host, and the read callbacks are invoked for data
received from the upstream host.
