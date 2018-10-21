.. _config_overview_v2:

Overview (v2 API)
=================

The Envoy v2 APIs are defined as `proto3
<https://developers.google.com/protocol-buffers/docs/proto3>`_ `Protocol Buffers
<https://developers.google.com/protocol-buffers/>`_ in the `data plane API
repository <https://github.com/envoyproxy/data-plane-api/tree/master/envoy/api>`_. They support

* Streaming delivery of `xDS <https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md>`_
  API updates via gRPC. This reduces resource requirements and can lower the update latency.
* A new REST-JSON API in which the JSON/YAML formats are derived mechanically via the `proto3
  canonical JSON mapping
  <https://developers.google.com/protocol-buffers/docs/proto3#json>`_.
* Delivery of updates via the filesystem, REST-JSON or gRPC endpoints.
* Advanced load balancing through an extended endpoint assignment API and load
  and resource utilization reporting to management servers.
* `Stronger consistency and ordering properties
  <https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md#eventual-consistency-considerations>`_
  when needed. The v2 APIs still maintain a baseline eventual consistency model.

See the `xDS protocol description <https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md>`_ for
further details on aspects of v2 message exchange between Envoy and the management server.

.. _config_overview_v2_bootstrap:

Bootstrap configuration
-----------------------

To use the v2 API, it's necessary to supply a bootstrap configuration file. This
provides static server configuration and configures Envoy to access :ref:`dynamic
configuration if needed <arch_overview_dynamic_config>`. This is supplied on the command-line via
the :option:`-c` flag, i.e.:

.. code-block:: console

  ./envoy -c <path to config>.{json,yaml,pb,pb_text}

where the extension reflects the underlying v2 config representation.

The :ref:`Bootstrap <envoy_api_msg_config.bootstrap.v2.Bootstrap>` message is the root of the
configuration. A key concept in the :ref:`Bootstrap <envoy_api_msg_config.bootstrap.v2.Bootstrap>`
message is the distinction between static and dynamic resouces. Resources such
as a :ref:`Listener <envoy_api_msg_Listener>` or :ref:`Cluster
<envoy_api_msg_Cluster>` may be supplied either statically in
:ref:`static_resources <envoy_api_field_config.bootstrap.v2.Bootstrap.static_resources>` or have
an xDS service such as :ref:`LDS
<config_listeners_lds>` or :ref:`CDS <config_cluster_manager_cds>` configured in
:ref:`dynamic_resources <envoy_api_field_config.bootstrap.v2.Bootstrap.dynamic_resources>`.

Example
-------

Below we will use YAML representation of the config protos and a running example
of a service proxying HTTP from 127.0.0.1:10000 to 127.0.0.2:1234.

Static
^^^^^^

A minimal fully static bootstrap config is provided below:

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address: { address: 127.0.0.1, port_value: 10000 }
      filter_chains:
      - filters:
        - name: envoy.http_connection_manager
          config:
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
            - name: envoy.router
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
discovery <arch_overview_dynamic_config_sds>` via an
:ref:`EDS<envoy_api_file_envoy/api/v2/eds.proto>` gRPC management server listening
on 127.0.0.3:5678 is provided below:

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address: { address: 127.0.0.1, port_value: 10000 }
      filter_chains:
      - filters:
        - name: envoy.http_connection_manager
          config:
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
            - name: envoy.router
    clusters:
    - name: some_service
      connect_timeout: 0.25s
      lb_policy: ROUND_ROBIN
      type: EDS
      eds_cluster_config:
        eds_config:
          api_config_source:
            api_type: GRPC
            grpc_services:
              envoy_grpc:
                cluster_name: xds_cluster
    - name: xds_cluster
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {}
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

In the above example, the EDS management server could then return a proto encoding of a
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.api.v2.ClusterLoadAssignment
    cluster_name: some_service
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: 127.0.0.2
              port_value: 1234


The versioning and type URL scheme that appear above are explained in more
detail in the `streaming gRPC subscription protocol
<https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md#streaming-grpc-subscriptions>`_
documentation.

Dynamic
^^^^^^^

A fully dynamic bootstrap configuration, in which all resources other than
those belonging to the management server are discovered via xDS is provided
below:

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 127.0.0.1, port_value: 9901 }

  dynamic_resources:
    lds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: xds_cluster
    cds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: xds_cluster

  static_resources:
    clusters:
    - name: xds_cluster
      connect_timeout: 0.25s
      type: STATIC
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {}
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
  - "@type": type.googleapis.com/envoy.api.v2.Listener
    name: listener_0
    address:
      socket_address:
        address: 127.0.0.1
        port_value: 10000
    filter_chains:
    - filters:
      - name: envoy.http_connection_manager
        config:
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
          - name: envoy.router

The management server could respond to RDS requests with:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.api.v2.RouteConfiguration
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
  - "@type": type.googleapis.com/envoy.api.v2.Cluster
    name: some_service
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    type: EDS
    eds_cluster_config:
      eds_config:
        api_config_source:
          api_type: GRPC
          grpc_services:
            envoy_grpc:
              cluster_name: xds_cluster

The management server could respond to EDS requests with:

.. code-block:: yaml

  version_info: "0"
  resources:
  - "@type": type.googleapis.com/envoy.api.v2.ClusterLoadAssignment
    cluster_name: some_service
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: 127.0.0.2
              port_value: 1234

Upgrading from v1 configuration
-------------------------------

While new v2 bootstrap JSON/YAML can be written, it might be expedient to upgrade an existing
v1 JSON/YAML configuration to v2. To do this (in an Envoy source tree),
you can run:

.. code-block:: console

  bazel run //tools:v1_to_bootstrap <path to v1 JSON/YAML configuration file>

.. _config_overview_v2_management_server:

Management server
-----------------

A v2 xDS management server will implement the below endpoints as required for
gRPC and/or REST serving. In both streaming gRPC and
REST-JSON cases, a :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` is sent and a
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` received following the
`xDS protocol <https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md>`_.

.. _v2_grpc_streaming_endpoints:

gRPC streaming endpoints
^^^^^^^^^^^^^^^^^^^^^^^^

.. http:post:: /envoy.api.v2.ClusterDiscoveryService/StreamClusters

See `cds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/cds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    cds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

is set in the :ref:`dynamic_resources
<envoy_api_field_config.bootstrap.v2.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_api_msg_config.bootstrap.v2.Bootstrap>` config.

.. http:post:: /envoy.api.v2.EndpointDiscoveryService/StreamEndpoints

See `eds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/eds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    eds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

is set in the :ref:`eds_cluster_config
<envoy_api_field_Cluster.eds_cluster_config>` field of the :ref:`Cluster
<envoy_api_msg_Cluster>` config.

.. http:post:: /envoy.api.v2.ListenerDiscoveryService/StreamListeners

See `lds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/lds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    lds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

is set in the :ref:`dynamic_resources
<envoy_api_field_config.bootstrap.v2.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_api_msg_config.bootstrap.v2.Bootstrap>` config.

.. http:post:: /envoy.api.v2.RouteDiscoveryService/StreamRoutes

See `rds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/rds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    route_config_name: some_route_name
    config_source:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

is set in the :ref:`rds
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.rds>` field of the :ref:`HttpConnectionManager
<envoy_api_msg_config.filter.network.http_connection_manager.v2.HttpConnectionManager>` config.

REST endpoints
^^^^^^^^^^^^^^

.. http:post:: /v2/discovery:clusters

See `cds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/cds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    cds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`dynamic_resources
<envoy_api_field_config.bootstrap.v2.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_api_msg_config.bootstrap.v2.Bootstrap>` config.

.. http:post:: /v2/discovery:endpoints

See `eds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/eds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    eds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`eds_cluster_config
<envoy_api_field_Cluster.eds_cluster_config>` field of the :ref:`Cluster
<envoy_api_msg_Cluster>` config.

.. http:post:: /v2/discovery:listeners

See `lds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/lds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    lds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`dynamic_resources
<envoy_api_field_config.bootstrap.v2.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_api_msg_config.bootstrap.v2.Bootstrap>` config.

.. http:post:: /v2/discovery:routes

See `rds.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/rds.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    route_config_name: some_route_name
    config_source:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`rds
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.rds>` field of the :ref:`HttpConnectionManager
<envoy_api_msg_config.filter.network.http_connection_manager.v2.HttpConnectionManager>` config.

.. _config_overview_v2_ads:

Aggregated Discovery Service
----------------------------

While Envoy fundamentally employs an eventual consistency model, ADS provides an
opportunity to sequence API update pushes and ensure affinity of a single
management server for an Envoy node for API updates. ADS allows one or more APIs
and their resources to be delivered on a single, bidirectional gRPC stream by
the management server. Without this, some APIs such as RDS and EDS may require
the management of multiple streams and connections to distinct management
servers.

ADS will allow for hitless updates of configuration by appropriate sequencing.
For example, suppose *foo.com* was mappped to cluster *X*. We wish to change the
mapping in the route table to point *foo.com* at cluster *Y*. In order to do
this, a CDS/EDS update must first be delivered containing both clusters *X* and
*Y*.

Without ADS, the CDS/EDS/RDS streams may point at distinct management servers,
or when on the same management server at distinct gRPC streams/connections that
require coordination. The EDS resource requests may be split across two distinct
streams, one for *X* and one for *Y*. ADS allows these to be coalesced to a
single stream to a single management server, avoiding the need for distributed
synchronization to correctly sequence the update. With ADS, the management
server would deliver the CDS, EDS and then RDS updates on a single stream.

ADS is only available for gRPC streaming (not REST) and is described more fully
in `this
<https://github.com/envoyproxy/data-plane-api/blob/master/XDS_PROTOCOL.md#aggregated-discovery-services-ads>`_
document. The gRPC endpoint is:

.. http:post:: /envoy.api.v2.AggregatedDiscoveryService/StreamAggregatedResources

See `discovery.proto
<https://github.com/envoyproxy/data-plane-api/blob/master/envoy/api/v2/discovery.proto>`_
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    ads_config:
      api_type: GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: some_ads_cluster

is set in the :ref:`dynamic_resources
<envoy_api_field_config.bootstrap.v2.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_api_msg_config.bootstrap.v2.Bootstrap>` config.

When this is set, any of the configuration sources :ref:`above <v2_grpc_streaming_endpoints>` can
be set to use the ADS channel. For example, a LDS config could be changed from

.. code-block:: yaml

    lds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

to

.. code-block:: yaml

    lds_config: {ads: {}}

with the effect that the LDS stream will be directed to *some_ads_cluster* over
the shared ADS channel.

.. _config_overview_v2_mgmt_con_issues:

Management Server Unreachability
--------------------------------

When an Envoy instance loses connectivity with the management server, Envoy will latch on to
the previous configuration while actively retrying in the background to reestablish the
connection with the management server.

Envoy debug logs the fact that it is not able to establish a connection with the management server
every time it attempts a connection.

:ref:`connected_state <management_server_stats>` statistic provides a signal for monitoring this behavior.

.. _management_server_stats:

Statistics
----------

Management Server has a statistics tree rooted at *control_plane.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   connected_state, Gauge, A boolan (1 for connected and 0 for disconnected) that indicates the current connection state with management server

.. _config_overview_v2_status:

Status
------

All features described in the :ref:`v2 API reference <envoy_api_reference>` are
implemented unless otherwise noted. In the v2 API reference and the
`v2 API repository
<https://github.com/envoyproxy/data-plane-api/tree/master>`_, all protos are
*frozen* unless they are tagged as *draft* or *experimental*. Here, *frozen*
means that we will not break wire format compatibility.

*Frozen* protos may be further extended, e.g. by adding new fields, in a
manner that does not break `backwards compatibility
<https://developers.google.com/protocol-buffers/docs/overview#how-do-they-work>`_.
Fields in the above protos may be later deprecated, subject to the
`breaking change policy
<https://github.com/envoyproxy/envoy/blob/master//CONTRIBUTING.md#breaking-change-policy>`_,
when their related functionality is no longer required. While frozen APIs
have their wire format compatibility preserved, we reserve the right to change
proto namespaces, file locations and nesting relationships, which may cause
breaking code changes. We will aim to minimize the churn here.

Protos tagged *draft*, meaning that they are near finalized, are
likely to be at least partially implemented in Envoy but may have wire format
breaking changes made prior to freezing.

Protos tagged *experimental*, have the same caveats as draft protos
and may have have major changes made prior to Envoy implementation and freezing.

The current open v2 API issues are tracked `here
<https://github.com/envoyproxy/envoy/issues?q=is%3Aopen+is%3Aissue+label%3A%22v2+API%22>`_.
