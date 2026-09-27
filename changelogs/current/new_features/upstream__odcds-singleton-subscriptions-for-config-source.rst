Added support for creating a separate subscription for each on-demand discovered cluster when
on-demand CDS is configured with a regular (non-ADS) config source, similar to ADS and xDS-TP based
config sources. Note that for a gRPC config source this results in a separate gRPC stream per
cluster. This is used by the :ref:`on-demand HTTP filter <config_http_filters_on_demand>` and the
:ref:`TCP proxy <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy.OnDemand>`, and
can be enabled by setting the runtime guard
``envoy.reloadable_features.odcds_singleton_subscriptions_for_config_source`` to ``true``.
