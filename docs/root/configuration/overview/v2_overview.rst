.. _config_overview_v2:

Overview
========

The Envoy v2 APIs are defined as `proto3
<https://developers.google.com/protocol-buffers/docs/proto3>`_ `Protocol Buffers
<https://developers.google.com/protocol-buffers/>`_ in the :repo:`api tree <api/>`. They
support:

* Streaming delivery of :ref:`xDS <xds_protocol>` API updates via gRPC. This reduces
  resource requirements and can lower the update latency.
* A new REST-JSON API in which the JSON/YAML formats are derived mechanically via the `proto3
  canonical JSON mapping
  <https://developers.google.com/protocol-buffers/docs/proto3#json>`_.
* Delivery of updates via the filesystem, REST-JSON or gRPC endpoints.
* Advanced load balancing through an extended endpoint assignment API and load
  and resource utilization reporting to management servers.
* :ref:`Stronger consistency and ordering properties
  <xds_protocol_eventual_consistency_considerations>`
  when needed. The v2 APIs still maintain a baseline eventual consistency model.

See the :ref:`xDS protocol description <xds_protocol>` for
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
message is the distinction between static and dynamic resources. Resources such
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
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
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
discovery <arch_overview_dynamic_config_eds>` via an
:ref:`EDS<envoy_api_file_envoy/api/v2/eds.proto>` gRPC management server listening
on 127.0.0.1:5678 is provided below:

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
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
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

It's important to set appropriate :ref:`TCP Keep-Alive options <envoy_api_msg_core.TcpKeepalive>`
in the `tcp_keepalive` block. This will help detect TCP half open connections to the xDS management
server and re-establish a full connection.

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
detail in the :ref:`streaming gRPC subscription protocol
<xds_protocol_streaming_grpc_subscriptions>`
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

.. _config_overview_v2_management_server:

xDS API endpoints
-----------------

A v2 xDS management server will implement the below endpoints as required for
gRPC and/or REST serving. In both streaming gRPC and
REST-JSON cases, a :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` is sent and a
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` received following the
:ref:`xDS protocol <xds_protocol>`.

.. _v2_grpc_streaming_endpoints:

gRPC streaming endpoints
^^^^^^^^^^^^^^^^^^^^^^^^

.. http:post:: /envoy.api.v2.ClusterDiscoveryService/StreamClusters

See :repo:`cds.proto <api/envoy/api/v2/cds.proto>` for the service definition. This is used by Envoy
as a client when

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

See :repo:`eds.proto
<api/envoy/api/v2/eds.proto>`
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

See :repo:`lds.proto
<api/envoy/api/v2/lds.proto>`
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

See :repo:`rds.proto
<api/envoy/api/v2/rds.proto>`
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
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.rds>` field
of the :ref:`HttpConnectionManager
<envoy_api_msg_config.filter.network.http_connection_manager.v2.HttpConnectionManager>` config.

.. http:post:: /envoy.api.v2.ScopedRoutesDiscoveryService/StreamScopedRoutes

See :repo:`srds.proto
<api/envoy/api/v2/srds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    name: some_scoped_route_name
    scoped_rds:
      config_source:
        api_config_source:
          api_type: GRPC
          grpc_services:
            envoy_grpc:
              cluster_name: some_xds_cluster

is set in the :ref:`scoped_routes
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.scoped_routes>`
field of the :ref:`HttpConnectionManager
<envoy_api_msg_config.filter.network.http_connection_manager.v2.HttpConnectionManager>` config.

.. http:post:: /envoy.service.discovery.v2.SecretDiscoveryService/StreamSecrets

See :repo:`sds.proto
<api/envoy/service/discovery/v2/srds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    name: some_secret_name
    config_source:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

is set inside a :ref:`SdsSecretConfig <envoy_api_msg_auth.SdsSecretConfig>` message. This message
is used in various places such as the :ref:`CommonTlsContext <envoy_api_msg_auth.CommonTlsContext>`.

.. http:post:: /envoy.service.discovery.v2.RuntimeDiscoveryService/StreamRuntime

See :repo:`rtds.proto
<api/envoy/service/discovery/v2/rtds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    name: some_runtime_layer_name
    config_source:
      api_config_source:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: some_xds_cluster

is set inside the :ref:`rtds_layer <envoy_api_field_config.bootstrap.v2.RuntimeLayer.rtds_layer>`
field.

REST endpoints
^^^^^^^^^^^^^^

.. http:post:: /v2/discovery:clusters

See :repo:`cds.proto
<api/envoy/api/v2/cds.proto>`
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

See :repo:`eds.proto
<api/envoy/api/v2/eds.proto>`
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

See :repo:`lds.proto
<api/envoy/api/v2/lds.proto>`
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

See :repo:`rds.proto
<api/envoy/api/v2/rds.proto>`
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

.. note::

    The management server responding to these endpoints must respond with a :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`
    along with a HTTP status of 200. Additionally, if the configuration that would be supplied has not changed (as indicated by the version
    supplied by the Envoy client) then the management server can respond with an empty body and a HTTP status of 304.

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
For example, suppose *foo.com* was mapped to cluster *X*. We wish to change the
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
in :ref:`xDS <xds_protocol_ads>`
document. The gRPC endpoint is:

.. http:post:: /envoy.service.discovery.v2.AggregatedDiscoveryService/StreamAggregatedResources

See :repo:`discovery.proto
<api/envoy/api/v2/discovery.proto>`
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

.. _config_overview_v2_delta:

Delta endpoints
---------------

The REST, filesystem, and original gRPC xDS implementations all deliver "state of the world" updates:
every CDS update must contain every cluster, with the absence of a cluster from an update implying
that the cluster is gone. For Envoy deployments with huge amounts of resources and even a trickle of
churn, these state-of-the-world updates can be cumbersome.

As of 1.12.0, Envoy supports a "delta" variant of xDS (including ADS), where updates only contain
resources added/changed/removed. Delta xDS is a gRPC (only) protocol. Delta uses different
request/response protos than SotW (DeltaDiscovery{Request,Response}); see
:repo:`discovery.proto <api/envoy/api/v2/discovery.proto>`. Conceptually, delta should be viewed as
a new xDS transport type: there is static, filesystem, REST, gRPC-SotW, and now gRPC-delta.
(Envoy's implementation of the gRPC-SotW/delta client happens to share most of its code between the
two, and something similar is likely possible on the server side. However, they are in fact
incompatible protocols.
:ref:`The specification of the delta xDS protocol's behavior is here <xds_protocol_delta>`.)

To use delta, simply set the api_type field of your
:ref:`ApiConfigSource <envoy_api_msg_core.ApiConfigSource>` proto(s) to DELTA_GRPC.
That works for both xDS and ADS; for ADS, it's the api_type field of
:ref:`DynamicResources.ads_config <envoy_api_field_config.bootstrap.v2.Bootstrap.dynamic_resources>`,
as described in the previous section.

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

   connected_state, Gauge, A boolean (1 for connected and 0 for disconnected) that indicates the current connection state with management server
   rate_limit_enforced, Counter, Total number of times rate limit was enforced for management server requests
   pending_requests, Gauge, Total number of pending requests when the rate limit was enforced

.. _subscription_statistics:

xDS subscription statistics
---------------------------

Envoy discovers its various dynamic resources via discovery
services referred to as *xDS*. Resources are requested via :ref:`subscriptions <xds_protocol>`,
by specifying a filesystem path to watch, initiating gRPC streams or polling a REST-JSON URL.

The following statistics are generated for all subscriptions.

.. csv-table::
 :header: Name, Type, Description
 :widths: 1, 1, 2

 config_reload, Counter, Total API fetches that resulted in a config reload due to a different config
 init_fetch_timeout, Counter, Total :ref:`initial fetch timeouts <envoy_api_field_core.ConfigSource.initial_fetch_timeout>`
 update_attempt, Counter, Total API fetches attempted
 update_success, Counter, Total API fetches completed successfully
 update_failure, Counter, Total API fetches that failed because of network errors
 update_rejected, Counter, Total API fetches that failed because of schema/validation errors
 version, Gauge, Hash of the contents from the last successful API fetch
 control_plane.connected_state, Gauge, A boolean (1 for connected and 0 for disconnected) that indicates the current connection state with management server
