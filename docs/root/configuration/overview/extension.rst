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
    "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
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
        "@type": type.googleapis.com/envoy.config.filter.http.router.v2.Router
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
    type_url: type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
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
          type_url: type.googleapis.com/envoy.config.filter.http.router.v2.Router

