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
* :ref:`Request ID <arch_overview_tracing>`
* Transport sockets
* BoringSSL private key methods
* :ref:`Watchdog action <envoy_v3_api_msg_config.bootstrap.v3.Watchdog.WatchdogAction>`
* :ref:`Internal redirect policy <envoy_v3_api_field_config.route.v3.InternalRedirectPolicy.predicates>`
* :ref:`Compression libraries <arch_overview_compression_libraries>`
* :ref:`Bootstrap extensions <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.bootstrap_extensions>`
* :ref:`Fatal actions <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.fatal_actions>`
* :ref:`Formatters <config_access_log_command_operators>`
* :ref:`Connection balance extensions <envoy_v3_api_field_config.listener.v3.Listener.ConnectionBalanceConfig.extend_balance>`

As of this writing there is no high level extension developer documentation. The
:repo:`existing extensions <source/extensions>` are a good way to learn what is possible.

An example of how to add a network filter and structure the repository and build dependencies can
be found at `envoy-filter-example <https://github.com/envoyproxy/envoy-filter-example>`_.
