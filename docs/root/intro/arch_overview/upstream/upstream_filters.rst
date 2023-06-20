.. _arch_overview_upstream_filters:

Upstream network filters
========================

Upstream clusters provide an ability to inject network level (L3/L4)
filters. It should be noted that a network filter needs to
be registered in code as an upstream filter before usage. Currently,
there are no upstream filters available in Envoy out of the box.
The filters apply to the connection to the upstream hosts, using the same API presented by listeners for
the downstream connections. The write-callbacks are invoked for any chunk of
data sent to the upstream host, and the read-callbacks are invoked for data
received from the upstream host.
