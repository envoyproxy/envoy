.. _arch_overview_overload_manager:

Overload manager
================

The overload manager is an extensible component for protecting the Envoy server from overload
with respect to various system resources (such as memory, cpu or file descriptors) due to too
many client connections or requests. This is distinct from
:ref:`circuit breaking <arch_overview_circuit_break>` which is primarily aimed at protecting
upstream services.

The overload manager is :ref:`configured <config_overload_manager>` by specifying a set of
resources to monitor and a set of overload actions that will be taken when some of those
resources exceed certain pressure thresholds.
