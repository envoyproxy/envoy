.. _arch_overview_tcp_proxy:

TCP proxy
=========

Since Envoy is fundamentally written as a L3/L4 server, basic L3/L4 proxy is easily implemented. The
TCP proxy filter performs basic 1:1 network connection proxy between downstream clients and upstream
clusters. It can be used by itself as an stunnel replacement, or in conjunction with other filters
such as the :ref:`MongoDB filter <arch_overview_mongo>` or the :ref:`rate limit
<config_network_filters_rate_limit>` filter.

TCP proxy filter :ref:`configuration reference <config_network_filters_tcp_proxy>`.
