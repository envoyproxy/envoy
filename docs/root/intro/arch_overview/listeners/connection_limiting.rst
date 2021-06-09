.. _arch_overview_connection_limit:

Connection limiting
===================

Envoy supports local (non-distributed) connection limiting of L4 connections via the
:ref:`Connection limit filter <config_network_filters_connection_limit>` and runtime
connection limiting via the :ref:`Runtime listener connection limit <config_listeners_runtime>`.
