.. _arch_overview_local_rate_limit:

Local rate limiting
===================

Envoy supports local (non-distributed) rate limiting of L4 connections via the
:ref:`local rate limit filter <config_network_filters_local_rate_limit>`.

Note that Envoy also supports :ref:`global rate limiting <arch_overview_global_rate_limit>`. Local
rate limiting can be used in conjunction with global rate limiting to reduce load on the global
rate limit service.
