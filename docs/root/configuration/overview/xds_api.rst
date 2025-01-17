.. _config_overview_management_server:

xDS API endpoints
-----------------

An xDS management server will implement the below endpoints as required for
gRPC and/or REST serving. In both streaming gRPC and
REST-JSON cases, a :ref:`DiscoveryRequest <envoy_v3_api_msg_service.discovery.v3.DiscoveryRequest>` is sent and a
:ref:`DiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.DiscoveryResponse>` received following the
:ref:`xDS protocol <xds_protocol>`.

Below we describe endpoints for the v3 transport API.

.. _v3_grpc_streaming_endpoints:

gRPC streaming endpoints
^^^^^^^^^^^^^^^^^^^^^^^^

.. http:post:: /envoy.service.cluster.v3.ClusterDiscoveryService/StreamClusters

See :repo:`cds.proto <api/envoy/service/cluster/v3/cds.proto>` for the service definition. This is used by Envoy
as a client when

.. literalinclude:: _include/xds_api/dynamic-resources.yaml
   :language: yaml
   :lines: 12-19
   :linenos:
   :lineno-start: 12
   :caption: :download:`xds-api-cds-demo.yaml <_include/xds_api/dynamic-resources.yaml>`

is set in the :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` config.

.. http:post:: /envoy.service.endpoint.v3.EndpointDiscoveryService/StreamEndpoints

See :repo:`eds.proto
<api/envoy/service/endpoint/v3/eds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    eds_config:
      api_config_source:
        api_type: GRPC
        grpc_services:
        - envoy_grpc:
            cluster_name: some_xds_cluster

is set in the :ref:`eds_cluster_config
<envoy_v3_api_field_config.cluster.v3.Cluster.eds_cluster_config>` field of the :ref:`Cluster
<envoy_v3_api_msg_config.cluster.v3.Cluster>` config.

.. http:post:: /envoy.service.listener.v3.ListenerDiscoveryService/StreamListeners

See :repo:`lds.proto
<api/envoy/service/listener/v3/lds.proto>`
for the service definition. This is used by Envoy as a client when

.. literalinclude:: _include/xds_api/dynamic-resources.yaml
   :language: yaml
   :lines: 20-27
   :linenos:
   :lineno-start: 20
   :caption: :download:`xds-api-lds-demo.yaml <_include/xds_api/dynamic-resources.yaml>`

is set in the :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` config.

.. http:post:: /envoy.service.route.v3.RouteDiscoveryService/StreamRoutes

See :repo:`rds.proto
<api/envoy/service/route/v3/rds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    route_config_name: some_route_name
    config_source:
      api_config_source:
        api_type: GRPC
        grpc_services:
        - envoy_grpc:
            cluster_name: some_xds_cluster

is set in the :ref:`rds
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.rds>` field
of the :ref:`HttpConnectionManager
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>` config.

.. http:post:: /envoy.service.route.v3.ScopedRoutesDiscoveryService/StreamScopedRoutes

See :repo:`srds.proto
<api/envoy/service/route/v3/srds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    name: some_scoped_route_name
    scoped_rds:
      config_source:
        api_config_source:
          api_type: GRPC
          grpc_services:
          - envoy_grpc:
              cluster_name: some_xds_cluster

is set in the :ref:`scoped_routes
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.scoped_routes>`
field of the :ref:`HttpConnectionManager
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>` config.

.. http:post:: /envoy.service.secret.v3.SecretDiscoveryService/StreamSecrets

See :repo:`sds.proto
<api/envoy/service/secret/v3/sds.proto>`
for the service definition. This is used by Envoy as a client when

.. literalinclude:: _include/xds_api/oauth-sds-example.yaml
   :language: yaml
   :lines: 48-57
   :linenos:
   :lineno-start: 48
   :caption: :download:`oauth-sds-example.yaml <_include/xds_api/oauth-sds-example.yaml>`

is set inside a :ref:`SdsSecretConfig <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.SdsSecretConfig>` message. This message
is used in various places such as the :ref:`CommonTlsContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CommonTlsContext>`.

.. http:post:: /envoy.service.runtime.v3.RuntimeDiscoveryService/StreamRuntime

See :repo:`rtds.proto
<api/envoy/service/runtime/v3/rtds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    name: some_runtime_layer_name
    config_source:
      api_config_source:
        api_type: GRPC
        grpc_services:
        - envoy_grpc:
            cluster_name: some_xds_cluster

is set inside the :ref:`rtds_layer <envoy_v3_api_field_config.bootstrap.v3.RuntimeLayer.rtds_layer>`
field.


REST endpoints
^^^^^^^^^^^^^^

.. http:post:: /v3/discovery:clusters

See :repo:`cds.proto
<api/envoy/service/cluster/v3/cds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    cds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` config.

.. http:post:: /v3/discovery:endpoints

See :repo:`eds.proto
<api/envoy/service/endpoint/v3/eds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    eds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`eds_cluster_config
<envoy_v3_api_field_config.cluster.v3.Cluster.eds_cluster_config>` field of the :ref:`Cluster
<envoy_v3_api_msg_config.cluster.v3.Cluster>` config.

.. http:post:: /v3/discovery:listeners

See :repo:`lds.proto
<api/envoy/service/listener/v3/lds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    lds_config:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` config.

.. http:post:: /v3/discovery:routes

See :repo:`rds.proto
<api/envoy/service/route/v3/rds.proto>`
for the service definition. This is used by Envoy as a client when

.. code-block:: yaml

    route_config_name: some_route_name
    config_source:
      api_config_source:
        api_type: REST
        cluster_names: [some_xds_cluster]

is set in the :ref:`rds
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.rds>` field of the :ref:`HttpConnectionManager
<envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>` config.

.. note::

    The management server responding to these endpoints must respond with a :ref:`DiscoveryResponse <envoy_v3_api_msg_service.discovery.v3.discoveryresponse>`
    along with a HTTP status of 200. Additionally, if the configuration that would be supplied has not changed (as indicated by the version
    supplied by the Envoy client) then the management server can respond with an empty body and a HTTP status of 304.

.. _config_overview_ads:

Aggregated Discovery Service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

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

.. http:post:: /envoy.service.discovery.v3.AggregatedDiscoveryService/StreamAggregatedResources

See :repo:`discovery.proto
<api/envoy/service/discovery/v3/discovery.proto>`
for the service definition. This is used by Envoy as a client when

.. literalinclude:: _include/xds_api/dynamic-resources.yaml
   :language: yaml
   :lines: 6-11
   :linenos:
   :lineno-start: 6
   :caption: :download:`xds-api-ads-demo.yaml <_include/xds_api/dynamic-resources.yaml>`

is set in the :ref:`dynamic_resources
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>` of the :ref:`Bootstrap
<envoy_v3_api_msg_config.bootstrap.v3.Bootstrap>` config.

When this is set, any of the configuration sources :ref:`above <v3_grpc_streaming_endpoints>` can
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

.. _config_overview_delta:

Delta endpoints
^^^^^^^^^^^^^^^

The REST, filesystem, and original gRPC xDS implementations all deliver "state of the world" updates:
every CDS update must contain every cluster, with the absence of a cluster from an update implying
that the cluster is gone. For Envoy deployments with huge amounts of resources and even a trickle of
churn, these state-of-the-world updates can be cumbersome.

As of 1.12.0, Envoy supports a "delta" variant of xDS (including ADS), where updates only contain
resources added/changed/removed. Delta xDS is a gRPC (only) protocol. Delta uses different
request/response protos than SotW (DeltaDiscovery{Request,Response}); see
:repo:`discovery.proto <api/envoy/service/discovery/v3/discovery.proto>`. Conceptually, delta should be viewed as
a new xDS transport type: there is static, filesystem, REST, gRPC-SotW, and now gRPC-delta.
(Envoy's implementation of the gRPC-SotW/delta client happens to share most of its code between the
two, and something similar is likely possible on the server side. However, they are in fact
incompatible protocols.
:ref:`The specification of the delta xDS protocol's behavior is here <xds_protocol_delta>`.)

To use delta, simply set the api_type field of your
:ref:`ApiConfigSource <envoy_v3_api_msg_config.core.v3.ApiConfigSource>` proto(s) to DELTA_GRPC.
That works for both xDS and ADS; for ADS, it's the api_type field of
:ref:`DynamicResources.ads_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.dynamic_resources>`,
as described in the previous section.


.. _config_overview_ttl:

xDS TTL
^^^^^^^

When using xDS, users might find themself wanting to temporarily update certain xDS resources. In order to do
safely, xDS TTLs can be used to make sure that if the control plane becomes unavailable and is unable to revert
the xDS change, Envoy will remove the resource after a TTL specified by the server. See the
:ref:`protocol documentation <xds_protocol_ttl>` for more information.

Currently the behavior when a TTL expires is that the resource is *removed* (as opposed to reverted to the
previous version). As such, this feature should primarily be used for use cases where the absence of the resource
is preferred instead of the temporary version, e.g. when using RTDS to apply a temporary runtime override.

The TTL is specified on the :ref:`Resource <envoy_v3_api_msg_service.discovery.v3.resource>` proto: for Delta xDS this is specified directly
within the response, while for SotW xDS the server may wrap individual resources listed in the response within a
:ref:`Resource <envoy_v3_api_msg_service.discovery.v3.resource>` in order to specify a TTL value.

The server can refresh or modify the TTL by issuing another response for the same version. In this case the resource
itself does not have to be included.
