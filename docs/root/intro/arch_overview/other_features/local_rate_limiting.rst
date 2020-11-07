.. _arch_overview_local_rate_limit:

Local rate limiting
===================

Envoy supports local (non-distributed) rate limiting of L4 connections via the
:ref:`local rate limit filter <config_network_filters_local_rate_limit>`.

Envoy additionally supports local rate limiting of HTTP requests via the
:ref:`HTTP local rate limit filter <config_http_filters_local_rate_limit>`. This can
be activated globally at the listener level or at a more specific level (e.g.: the virtual
host or route level).

Finally, Envoy also supports :ref:`global rate limiting <arch_overview_global_rate_limit>`. Local
rate limiting can be used in conjunction with global rate limiting to reduce load on the global
rate limit service.
