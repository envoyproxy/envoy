.. _config_http_filters_gcp_authn:

GCP Authentication Filter
=========================
This filter is used to fetch authentication tokens from GCP compute metadata server(https://cloud.google.com/run/docs/securing/service-identity#identity_tokens).
In multiple services architecture where these services likely need to communicate with each other, authenticating service-to-service(https://cloud.google.com/run/docs/authenticating/service-to-service) is required because many of these services may be private and require credentials for access.

Configuration
-------------
This filter should be configured with the name ``envoy.filters.http.gcp_authn``.

The filter configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.GcpAuthnFilterConfig>` has three fields:

* Field ``http_uri`` specifies the HTTP URI for fetching the from `GCE Metadata Server <https://cloud.google.com/compute/docs/metadata/overview>`_. The URL format is ``http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]``. The ``AUDIENCE`` field is provided by configuration, please see more details below.

* Field ``retry_policy`` specifies the retry policy if fetching tokens failed. This field is optional. If it is not configured, the filter will be fail-closed (i.e., reject the requests).

* Field ``cache_config`` specifies the configuration for the token cache which is used to avoid the duplicated queries to GCE metadata server for the same request.

The audience configuration :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.gcp_authn.v3.Audience>` is the URL of the destionation service, which is the receving service that calling service is invoking. This information is provided through cluster metadata :ref:`Metadata<envoy_v3_api_msg_config.core.v3.metadata>`

Coniguration example
--------------------
Static and dynamic resouce configuration example:

.. code-block:: yaml

  static_resources:
  clusters:
    // cluster for fake destination service which has typed metadata that contains 
    // the audience information. 
    - typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options:
              {}
      load_assignment:
        endpoints:
          - lb_endpoints:
              - endpoint:
                  address:
                    socket_address:
                      address: ::1
                      port_value: 36075
        cluster_name: cluster_0
      metadata:
        typed_filter_metadata:
          envoy.filters.http.gcp_authn:
            "@type": type.googleapis.com/envoy.extensions.filters.http.gcp_authn.v3.Audience
            url: http://test.com
      connect_timeout: 5s
      name: cluster_0
    // cluster for fake metadata server
    - typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options:
              {}
      load_assignment:
        endpoints:
          - lb_endpoints:
              - endpoint:
                  address:
                    socket_address:
                      address: ::1
                      port_value: 41713
        cluster_name: gcp_authn
      connect_timeout: 5s
      name: gcp_authn
  secrets:
    - name: secret_static_0
      tls_certificate:
        certificate_chain:
          inline_string: DUMMY_INLINE_BYTES
        private_key:
          inline_string: DUMMY_INLINE_BYTES
        password:
          inline_string: DUMMY_INLINE_BYTES
  dynamic_resources:
    lds_config:
      resource_api_version: V3
      path: /tmp/envoy_test_tmp.eth42V/170599_1647616221474515
  admin:
    access_log:
      - typed_config:
          "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
          path: /dev/null
        name: envoy.access_loggers.file
    address:
      socket_address:
        address: ::1
        port_value: 0
  layered_runtime:
    layers:
      - static_layer:
          {}
        name: static_layer
      - admin_layer:
          {}
        name: admin


Filter chain configuration example:

.. code-block:: yaml

  filter_chains:
      filters:
        name: "http"
        typed_config: [type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager]:
            codec_type: HTTP2
            stat_prefix: "config_test"
            route_config:
              name: "route_config_0"
              virtual_hosts:
                name: "integration"
                domains: "*"
                routes:
                  match:
                    prefix: "/"
                  route:
                    cluster: "cluster_0"
            http_filters:
              name: "envoy.filters.http.gcp_authn"
              typed_config: [type.googleapis.com/net.envoy.source.extensions.filters.http.metadata.GcpAuthnFilterConfig]:
                  http_uri:
                    uri: "http://metadata.google.internal/computeMetadata/v1/instance/service-accounts/default/identity?audience=[AUDIENCE]"
                    cluster: "gcp_authn"
                    timeout:
                      seconds: 10
            http_filters:
              name: "envoy.filters.http.router"
            access_log:
              name: "accesslog"
              filter:
                not_health_check_filter:
              typed_config:
                [type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog]:
                  path: "/dev/null"
            delayed_close_timeout:
              nanos: 100

