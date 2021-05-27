.. _arch_overview_dns_filter:

DNS Filter
==========

Envoy supports responding to DNS requests by configuring a :ref:`UDP listener DNS Filter
<config_udp_listener_filters_dns_filter>`.

The DNS filter supports responding to forward queries for A, AAAA and SRV records. The answers are
discovered from statically configured resources, clusters, or external DNS servers. By default the filter
will return DNS responses up to to 512 bytes. If domains are configured with multiple addresses,
or clusters with multiple endpoints, Envoy will return each discovered address up to the
aforementioned size limit. However for the external queries, the DNS resolver can be forced to use TCP
instead of UDP where the responses can go beyond 512 bytes limit.
