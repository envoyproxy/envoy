.. _config_overview_extension_configuration:

Extension configuration
-----------------------

Each configuration resource in Envoy has a type URL in the `typed_config`. This
type corresponds to a versioned schema. If the type URL uniquely identifies an
extension capable of interpreting the configuration, then the extension is
selected regardless of the `name` field. In this case the `name` field becomes
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
        api_config_source:
          api_type: GRPC
          grpc_services:
            envoy_grpc:
              cluster_name: xds_cluster
    http_filters:
    - name: front-router
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
        dynamic_stats: true

In case the control plane lacks the schema definitions for an extension,
`udpa.type.v1.TypedStruct` should be used as a generic container. The type URL
inside it is then used by a client to convert the contents to a typed
configuration resource. For example, the above example could be written as
follows:

.. code-block:: yaml

  name: front-http-proxy
  typed_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
    type_url: type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
    value:
      stat_prefix: ingress_http
      codec_type: AUTO
      rds:
        route_config_name: local_route
        config_source:
          api_config_source:
            api_type: GRPC
            grpc_services:
              envoy_grpc:
                cluster_name: xds_cluster
      http_filters:
      - name: front-router
        typed_config:
          "@type": type.googleapis.com/udpa.type.v1.TypedStruct
          type_url: type.googleapis.com/envoy.extensions.filters.http.router.v3Router

Discovery service
^^^^^^^^^^^^^^^^^

Extension configuration can be supplied dynamically from a :ref:`an xDS
management server<xds_protocol>` using :ref:`ExtensionConfiguration discovery
service<envoy_v3_api_file_envoy/service/extension/v3/config_discovery.proto>`.
The name field in the extension configuration acts as the resource identifier.
For example, HTTP connection manager supports :ref:`dynamic filter
re-configuration<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.config_discovery>`
for HTTP filters.

Extension config discovery service has a :ref:`statistics
<subscription_statistics>` tree rooted at
*<stat_prefix>.extension_config_discovery.<extension_config_name>.*. In addition
to the common subscription statistics, it also provides the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total number of successful configuration updates
  config_fail, Counter, Total number of failed configuration updates
