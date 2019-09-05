.. _extending:

Extending Envoy for custom use cases
====================================

The Envoy architecture makes it fairly easily extensible via a variety of different extension
types including:

* :ref:`Access loggers <arch_overview_access_logs>`
* :ref:`Access log filters <arch_overview_access_log_filters>`
* :ref:`Clusters <arch_overview_service_discovery>`
* :ref:`Listener filters <arch_overview_listener_filters>`
* :ref:`Network filters <arch_overview_network_filters>`
* :ref:`HTTP filters <arch_overview_http_filters>`
* :ref:`gRPC credential providers <arch_overview_grpc>`
* :ref:`Health checkers <arch_overview_health_checking>`
* :ref:`Resource monitors <arch_overview_overload_manager>`
* :ref:`Retry implementations <arch_overview_http_routing_retry>`
* :ref:`Stat sinks <arch_overview_statistics>`
* :ref:`Tracers <arch_overview_tracing>`
* Transport sockets
* BoringSSL private key methods

As of this writing there is no high level extension developer documentation. The
:repo:`existing extensions <source/extensions>` are a good way to learn what is possible.

An example of how to add a network filter and structure the repository and build dependencies can
be found at `envoy-filter-example <https://github.com/envoyproxy/envoy-filter-example>`_.
