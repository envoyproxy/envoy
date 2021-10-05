1.21.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* config: the ``--bootstrap-version`` CLI flag has been removed, Envoy has only been able to accept v3
  bootstrap configurations since 1.18.0.
* contrib: the :ref:`squash filter <config_http_filters_squash>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`kafka broker filter <config_network_filters_kafka_broker>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`RocketMQ proxy filter <config_network_filters_rocketmq_proxy>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`Postgres proxy filter <config_network_filters_postgres_proxy>` has been moved to
  :ref:`contrib images <install_contrib>`.
* contrib: the :ref:`MySQL proxy filter <config_network_filters_mysql_proxy>` has been moved to
  :ref:`contrib images <install_contrib>`.
* dns_filter: :ref:`dns_filter <envoy_v3_api_msg_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig>`
  protobuf fields have been renumbered to restore compatibility with Envoy
  1.18, breaking compatibility with Envoy 1.19.0 and 1.19.1. The new field
  numbering allows control planes supporting Envoy 1.18 to gracefully upgrade to
  :ref:`dns_resolution_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig.ClientContextConfig.dns_resolution_config>`,
  provided they skip over Envoy 1.19.0 and 1.19.1.
  Control planes upgrading from Envoy 1.19.0 and 1.19.1 will need to
  vendor the corresponding protobuf definitions to ensure that the
  renumbered fields have the types expected by those releases.
* ext_authz: fixed skipping authentication when returning either a direct response or a redirect. This behavior can be temporarily reverted by setting the ``envoy.reloadable_features.http_ext_authz_do_not_skip_direct_response_and_redirect`` runtime guard to false.
* extensions: deprecated extension names now default to triggering a configuration error.
  The previous warning-only behavior may be temporarily reverted by setting the runtime key
  ``envoy.deprecated_features.allow_deprecated_extension_names`` to true.
* xds: ``*`` became a reserved name for a wildcard resource that can be subscribed to and unsubscribed from at any time. This is a requirement for implementing the on-demand xDSes (like on-demand CDS) that can subscribe to specific resources next to their wildcard subscription. If such xDS is subscribed to both wildcard resource and to other specific resource, then in stream reconnection scenario, the xDS will not send an empty initial request, but a request containing ``*`` for wildcard subscription and the rest of the resources the xDS is subscribed to. If the xDS is only subscribed to wildcard resource, it will try to send a legacy wildcard request. This behavior implements the recent changes in :ref:`xDS protocol <xds_protocol>` and can be temporarily reverted by setting the ``envoy.restart_features.explicit_wildcard_resource`` runtime guard to false.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
