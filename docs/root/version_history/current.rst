1.21.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* listener: fixed the crash when updating listeners that do not bind to port.
* thrift_proxy: fix the thrift_proxy connection manager to correctly report success/error response metrics when performing :ref:`payload passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>`.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure`` and legacy code paths.

New Features
------------
* bootstrap: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>` in the bootstrap to support DNS resolver as an extension.
* cluster: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.cluster.v3.Cluster.typed_dns_resolver_config>` in the cluster to support DNS resolver as an extension.
* dns_cache: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.typed_dns_resolver_config>` in the dns_cache to support DNS resolver as an extension.
* dns_filter: added :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3.DnsFilterConfig.ClientContextConfig.typed_dns_resolver_config>` in the dns_filter to support DNS resolver as an extension.
* dns_resolver: added :ref:`CaresDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>` to support c-ares DNS resolver as an extension.
* dns_resolver: added :ref:`AppleDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>` to support apple DNS resolver as an extension.
* http: added support for :ref:`retriable health check status codes <envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.retriable_statuses>`.

Deprecated
----------
* bootstrap: :ref:`dns_resolution_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.typed_dns_resolver_config>`.
* cluster: :ref:`dns_resolution_config <envoy_v3_api_field_config.cluster.v3.Cluster.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_config.cluster.v3.Cluster.typed_dns_resolver_config>`.
* dns_cache: :ref:`dns_resolution_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_resolution_config>` is deprecated in favor of :ref:`typed_dns_resolver_config <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.typed_dns_resolver_config>`.
