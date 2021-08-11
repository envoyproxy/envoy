Examples
--------

Below we will use YAML representation of the config protos and a running example
of a service proxying HTTP from 127.0.0.1:10000 to 127.0.0.2:1234.

Static
^^^^^^

A minimal fully static bootstrap config is provided below:

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  admin:
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address: { address: 127.0.0.1, port_value: 10000 }
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: ingress_http
            codec_type: AUTO
            route_config:
              name: local_route
              virtual_hosts:
              - name: local_service
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: some_service }
            http_filters:
            - name: envoy.filters.http.router
    clusters:
    - name: some_service
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: some_service
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 1234

Mostly static with dynamic EDS
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A bootstrap config that continues from the above example with :ref:`dynamic endpoint
discovery <arch_overview_dynamic_config_eds>` via an
:ref:`EDS<envoy_v3_api_file_envoy/service/endpoint/v3/eds.proto>` gRPC management server listening
on 127.0.0.1:5678 is provided below:

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  admin:
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address: { address: 127.0.0.1, port_value: 10000 }
      filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: ingress_http
            codec_type: AUTO
            route_config:
              name: local_route
              virtual_hosts:
              - name: local_service
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: some_service }
            http_filters:
            - name: envoy.filters.http.router
    clusters:
    - name: some_service
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      type: EDS
      eds_cluster_config:
        eds_config:
          resource_api_version: V3
          api_config_source:
            api_type: GRPC
            transport_api_version: V3
            grpc_services:
              - envoy_grpc:
                  cluster_name: xds_cluster
    - name: xds_cluster
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options:
              connection_keepalive:
                interval: 30s
                timeout: 5s
      upstream_connection_options:
        # configure a TCP keep-alive to detect and reconnect to the admin
        # server in the event of a TCP socket half open connection
        tcp_keepalive: {}
      load_assignment:
        cluster_name: xds_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 5678

Notice above that *xds_cluster* is defined to point Envoy at the management server. Even in
an otherwise completely dynamic configurations, some static resources need to
be defined to point Envoy at its xDS management server(s).

It's important to set appropriate :ref:`TCP Keep-Alive options <envoy_v3_api_msg_config.core.v3.TcpKeepalive>`
in the ``tcp_keepalive`` block. This will help detect TCP half open connections to the xDS management
server and re-establish a full connection.

In the above example, the EDS management server could then return a proto encoding of a
:ref:`DiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.DiscoveryResponse>`:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment
    cluster_name: some_service
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: 127.0.0.2
              port_value: 1234


The versioning and type URL scheme that appear above are explained in more
detail in the :ref:`streaming gRPC subscription protocol
<xds_protocol_streaming_grpc_subscriptions>`
documentation.

Dynamic
^^^^^^^

A fully dynamic bootstrap configuration, in which all resources other than
those belonging to the management server are discovered via xDS is provided
below:

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  admin:
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  dynamic_resources:
    lds_config:
      resource_api_version: V3
      api_config_source:
        api_type: GRPC
        transport_api_version: V3
        grpc_services:
          - envoy_grpc:
              cluster_name: xds_cluster
    cds_config:
      resource_api_version: V3
      api_config_source:
        api_type: GRPC
        transport_api_version: V3
        grpc_services:
          - envoy_grpc:
              cluster_name: xds_cluster

  static_resources:
    clusters:
    - name: xds_cluster
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      typed_extension_protocol_options:
        envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
          "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
          explicit_http_config:
            http2_protocol_options:
              # Configure an HTTP/2 keep-alive to detect connection issues and reconnect
              # to the admin server if the connection is no longer responsive.
              connection_keepalive:
                interval: 30s
                timeout: 5s
      load_assignment:
        cluster_name: xds_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 5678

The management server could respond to LDS requests with:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.listener.v3.Listener
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 10000
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
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
                  - envoy_grpc:
                      cluster_name: xds_cluster
          http_filters:
          - name: envoy.filters.http.router

The management server could respond to RDS requests with:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.route.v3.RouteConfiguration
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/" }
        route: { cluster: some_service }

The management server could respond to CDS requests with:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.cluster.v3.Cluster
    name: some_service
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    type: EDS
    eds_cluster_config:
      eds_config:
        resource_api_version: V3
        api_config_source:
          api_type: GRPC
          transport_api_version: V3
          grpc_services:
            - envoy_grpc:
                cluster_name: xds_cluster

The management server could respond to EDS requests with:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment
    cluster_name: some_service
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: 127.0.0.2
              port_value: 1234

Special YAML usage
~~~~~~~~~~~~~~~~~~

When loading YAML configuration, the Envoy loader will interpret map keys tagged with !ignore
specially, and omit them entirely from the native configuration tree. Ordinarily, the YAML stream
must adhere strictly to the proto schemas defined for Envoy configuration. This allows content to
be declared that is explicitly handled as a non-represented type.

This lets you split your file into two parts: one in which we have YAML content not subject to
parsing according to the schema and another part that is parsed. YAML anchors in the first part
may be referenced by aliases in the second part. This mechanism can simplify setups that need to
re-use or dynamically generate configuration fragments.

See the following example:

.. literalinclude:: _include/tagged.yaml
    :language: yaml

.. warning::
    If you parse Envoy YAML configuration using external loaders, you may need to inform these
    loaders about the !ignore tag. Compliant YAML loaders will typically expose an interface to
    allow you to choose how to handle a custom tag.

For example, this will instruct `PyYAML <https://github.com/yaml/pyyaml>`_ to treat an ignored
node as a simple scalar when loading:

.. code-block:: python3

    yaml.SafeLoader.add_constructor('!ignore', yaml.loader.SafeConstructor.construct_scalar)

Alternatively, :repo:`this is how <tools/config_validation/validate_fragment.py>`
Envoy registers the !ignore tag in config validation.
