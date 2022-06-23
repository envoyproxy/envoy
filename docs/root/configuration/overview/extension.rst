.. _config_overview_extension_configuration:

Extension configuration
-----------------------

Each configuration resource in Envoy has a type URL in the ``typed_config``. This
type corresponds to a versioned schema. The type URL uniquely identifies an
extension capable of interpreting the configuration. The ``name`` field is
optional and can be used as an identifier or as an annotation for the
particular instance of the extension configuration. For example, the following
filter configuration snippet is permitted:

.. code-block:: yaml

  name: front-http-proxy
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    stat_prefix: ingress_http
    codec_type: AUTO
    rds:
      route_config_name: local_route
      config_source:
        resource_api_version: V3
        api_config_source:
          api_type: GRPC
          transport_api_version: V3
          grpc_services:
            envoy_grpc:
              cluster_name: xds_cluster
    http_filters:
    - name: front-router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
        dynamic_stats: true

In case the control plane lacks the schema definitions for an extension,
``xds.type.v3.TypedStruct`` should be used as a generic container. The type URL
inside it is then used by a client to convert the contents to a typed
configuration resource. For example, the above example could be written as
follows:

.. code-block:: yaml

  name: front-http-proxy
  typed_config:
    "@type": type.googleapis.com/xds.type.v3.TypedStruct
    type_url: type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    value:
      stat_prefix: ingress_http
      codec_type: AUTO
      rds:
        route_config_name: local_route
        resource_api_version: V3
        config_source:
          api_config_source:
            api_type: GRPC
            transport_api_version: V3
            grpc_services:
              envoy_grpc:
                cluster_name: xds_cluster
      http_filters:
      - name: front-router
        typed_config:
          "@type": type.googleapis.com/xds.type.v3.TypedStruct
          type_url: type.googleapis.com/envoy.extensions.filters.http.router.v3Router

.. _config_overview_extension_discovery:

Discovery service
^^^^^^^^^^^^^^^^^

Extension configuration can be supplied dynamically from an :ref:`xDS
management server<xds_protocol>` using :ref:`ExtensionConfiguration discovery
service<envoy_v3_api_file_envoy/service/extension/v3/config_discovery.proto>`.
The name field in the extension configuration acts as the resource identifier.

HTTP filters
^^^^^^^^^^^^

For HTTP filters, HTTP connection manager supports :ref:`dynamic filter
re-configuration<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.config_discovery>`.

HTTP filter extension config discovery service has a :ref:`statistics
<subscription_statistics>` tree rooted at
*extension_config_discovery.<stat_prefix>.<extension_config_name>*. For HTTP
filters, the value of *<stat_prefix>* is *http_filter*. In addition to the
common subscription statistics, it also provides the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total number of successful configuration updates
  config_fail, Counter, Total number of failed configuration updates
  config_conflict, Counter, Total number of conflicting applications of configuration updates; this may happen when a new listener cannot reuse a subscribed extension configuration due to an invalid type URL.

Listener filters
^^^^^^^^^^^^^^^^

For Listener filters, the discovery service configuration is: :ref:`dynamic listener filter
re-configuration<envoy_v3_api_field_config.listener.v3.ListenerFilter.config_discovery>`.
The dynamic listener filter config is only supported in TCP listeners.
If the dynamic config is missing, the connection will be rejected until a valid config is updated.

Listener filter extension config discovery service has a :ref:`statistics
<subscription_statistics>` tree rooted at listener.<address>. (or listener.<stat_prefix>. if :ref:`stat_prefix
<envoy_v3_api_field_config.listener.v3.Listener.stat_prefix>` is non-empty) with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  extension_config_missing, Counter, Total connections closed due to missing listener filter extension configuration
