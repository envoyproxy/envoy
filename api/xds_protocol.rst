.. _xds_protocol:

xDS REST and gRPC protocol
==========================

Envoy discovers its various dynamic resources via the filesystem or by
querying one or more management servers. Collectively, these discovery
services and their corresponding APIs are referred to as *xDS*.
Resources are requested via *subscriptions*, by specifying a filesystem
path to watch, initiating gRPC streams or polling a REST-JSON URL. The
latter two methods involve sending requests with a :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>`
proto payload. Resources are delivered in a
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`
proto payload in all methods. We discuss each type of subscription
below.

Filesystem subscriptions
------------------------

The simplest approach to delivering dynamic configuration is to place it
at a well known path specified in the :ref:`ConfigSource <envoy_api_msg_core.ConfigSource>`.
Envoy will use `inotify` (`kqueue` on macOS) to monitor the file for
changes and parse the 
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` proto in the file on update.
Binary protobufs, JSON, YAML and proto text are supported formats for
the 
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`.

There is no mechanism available for filesystem subscriptions to ACK/NACK
updates beyond stats counters and logs. The last valid configuration for
an xDS API will continue to apply if an configuration update rejection
occurs.

.. _xds_protocol_streaming_grpc_subscriptions:

Streaming gRPC subscriptions
----------------------------

Singleton resource type discovery
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A gRPC 
:ref:`ApiConfigSource <envoy_api_msg_core.ApiConfigSource>`
can be specified independently for each xDS API, pointing at an upstream
cluster corresponding to a management server. This will initiate an
independent bidirectional gRPC stream for each xDS resource type,
potentially to distinct management servers. API delivery is eventually
consistent. See <Aggregated Discovery Service>`
below for situations in which explicit control of sequencing is required.

Type URLs
^^^^^^^^^

Each xDS API is concerned with resources of a given type. There is a 1:1
correspondence between an xDS API and a resource type. That is:

-  LDS: :ref:`envoy.api.v2.Listener <envoy_api_msg_Listener>`
-  RDS: :ref:`envoy.api.v2.RouteConfiguration <envoy_api_msg_RouteConfiguration>`
-  VHDS: :ref:`envoy.api.v2.Vhds <envoy_api_msg_RouteConfiguration>`
-  CDS: :ref:`envoy.api.v2.Cluster <envoy_api_msg_Cluster>`
-  EDS: :ref:`envoy.api.v2.ClusterLoadAssignment <envoy_api_msg_ClusterLoadAssignment>`
-  SDS: :ref:`envoy.api.v2.Auth.Secret <envoy_api_msg_Auth.Secret>`
-  RTDS: :ref:`envoy.service.discovery.v2.Runtime <envoy_api_msg_service.discovery.v2.Runtime>`

The concept of `type URLs <https://developers.google.com/protocol-buffers/docs/proto3#any>`_ appears below, and takes the form
`type.googleapis.com/<resource type>`, e.g.
`type.googleapis.com/envoy.api.v2.Cluster` for CDS. In various
requests from Envoy and responses by the management server, the resource
type URL is stated.

ACK/NACK and versioning
^^^^^^^^^^^^^^^^^^^^^^^

Each stream begins with a 
:ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` from Envoy, specifying
the list of resources to subscribe to, the type URL corresponding to the
subscribed resources, the node identifier and an empty :ref:`version_info <envoy_api_field_DiscoveryRequest.version_info>`.
An example EDS request might be:

.. code:: yaml

    version_info:
    node: { id: envoy }
    resource_names:
    - foo
    - bar
    type_url: type.googleapis.com/envoy.api.v2.ClusterLoadAssignment
    response_nonce:

The management server may reply either immediately or when the requested
resources are available with a :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`, e.g.:

.. code:: yaml

    version_info: X
    resources:
    - foo ClusterLoadAssignment proto encoding
    - bar ClusterLoadAssignment proto encoding
    type_url: type.googleapis.com/envoy.api.v2.ClusterLoadAssignment
    nonce: A

After processing the :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`, Envoy will send a new
request on the stream, specifying the last version successfully applied
and the nonce provided by the management server. If the update was
successfully applied, the :ref:`version_info <envoy_api_field_DiscoveryResponse.version_info>` will be **X**, as indicated
in the sequence diagram:

.. figure:: diagrams/simple-ack.svg 
   :alt: Version update after ACK

In this sequence diagram, and below, the following format is used to abbreviate messages: 

- *DiscoveryRequest*: (V=version_info,R=resource_names,N=response_nonce,T=type_url)
- *DiscoveryResponse*: (V=version_info,R=resources,N=nonce,T=type_url)

The version provides Envoy and the management server a shared notion of
the currently applied configuration, as well as a mechanism to ACK/NACK
configuration updates. If Envoy had instead rejected configuration
update **X**, it would reply with :ref:`error_detail <envoy_api_field_DiscoveryRequest.error_detail>`
populated and its previous version, which in this case was the empty
initial version. The :ref:`error_detail <envoy_api_field_DiscoveryRequest.error_detail>` has more details around the exact
error message populated in the message field:

.. figure:: diagrams/simple-nack.svg
   :alt: No version update after NACK

Later, an API update may succeed at a new version **Y**:


.. figure:: diagrams/later-ack.svg
   :alt: ACK after NACK

Each stream has its own notion of versioning, there is no shared
versioning across resource types. When ADS is not used, even each
resource of a given resource type may have a distinct version, since the
Envoy API allows distinct EDS/RDS resources to point at different :ref:`ConfigSources <envoy_api_msg_core.ConfigSource>`.

.. _xds_protocol_resource_update:

Resource Update
~~~~~~~~~~~~~~~

When to send an update
^^^^^^^^^^^^^^^^^^^^^^

The management server should only send updates to the Envoy client when
the resources in the :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` have changed. Envoy replies
to any :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` with a :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` containing the
ACK/NACK immediately after it has been either accepted or rejected. If
the management server provides the same set of resources rather than
waiting for a change to occur, it will cause Envoy and the management
server to spin and have a severe performance impact.

Within a stream, new :ref:`DiscoveryRequests <envoy_api_msg_DiscoveryRequest>` supersede any prior
:ref:`DiscoveryRequests <envoy_api_msg_DiscoveryRequest>` having the same resource type. This means that
the management server only needs to respond to the latest
:ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` on each stream for any given resource type.

Resource hints
^^^^^^^^^^^^^^

The :ref:`resource_names <envoy_api_field_DiscoveryRequest.resource_names>` specified in the :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` are a hint.
Some resource types, e.g. `Clusters` and `Listeners` may
specify an empty :ref:`resource_names <envoy_api_field_DiscoveryRequest.resource_names>` list, since a client such as Envoy is interested in
learning about all the :ref:`Clusters (CDS) <envoy_api_msg_Cluster>` and :ref:`Listeners (LDS) <envoy_api_msg_Listener>`
that the management server(s) know about corresponding to its node
identification. Other resource types, e.g. :ref:`RouteConfiguration (RDS) <envoy_api_msg_RouteConfiguration>`
and :ref:`ClusterLoadAssignment (EDS) <envoy_api_msg_ClusterLoadAssignment>`, follow from earlier
CDS/LDS updates and Envoy is able to explicitly enumerate these
resources.

Envoy will always set the LDS/CDS resource hints to empty and it is expected that the management
server will provide the complete state of the LDS/CDS resources in each response. An absent
`Listener` or `Cluster` will be deleted. Other xDS clients may specify explicit LDS/CDS resources as
resource hints, for example if they only have a singleton listener and already know its name from
some out-of-band configuration.

For EDS/RDS, the management server does not need to supply every
requested resource and may also supply additional, unrequested
resources. :ref:`resource_names <envoy_api_field_DiscoveryRequest.resource_names>` is only a hint. Envoy will silently ignore
any superfluous resources. When a requested resource is missing in a RDS
or EDS update, Envoy will retain the last known value for this resource
except in the case where the `Cluster` or `Listener` is being
warmed. See <Resource Warming> section below on
the expectations during warming. The management server may be able to
infer all the required EDS/RDS resources from the :ref:`node <envoy_api_msg_Core.Node>`
identification in the :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>`, in which case this hint may
be discarded. An empty EDS/RDS :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` is effectively a
nop from the perspective of the respective resources in the Envoy.

When a `Listener` or `Cluster` is deleted, its corresponding EDS and
RDS resources are also deleted inside the Envoy instance. In order for
EDS resources to be known or tracked by Envoy, there must exist an
applied `Cluster` definition (e.g. sourced via CDS). A similar
relationship exists between RDS and `Listeners` (e.g. sourced via
LDS).

For EDS/RDS, Envoy may either generate a distinct stream for each
resource of a given type (e.g. if each :ref:`ConfigSource <envoy_api_msg_core.ConfigSource>` has its own
distinct upstream cluster for a management server), or may combine
together multiple resource requests for a given resource type when they
are destined for the same management server. While this is left to
implementation specifics, management servers should be capable of
handling one or more :ref:`resource_names <envoy_api_field_DiscoveryRequest.resource_names>` for a given resource type in
each request. Both sequence diagrams below are valid for fetching two
EDS resources `{foo, bar}`:

|Multiple EDS requests on the same stream| |Multiple EDS requests on
distinct streams|

Resource updates
^^^^^^^^^^^^^^^^

As discussed above, Envoy may update the list of :ref:`resource_names <envoy_api_field_DiscoveryRequest.resource_names>` it
presents to the management server in each :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` that
ACK/NACKs a specific :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`. In addition, Envoy may later
issue additional :ref:`DiscoveryRequests <envoy_api_msg_DiscoveryRequest>` at a given :ref:`version_info <envoy_api_field_DiscoveryRequest.version_info>` to
update the management server with new resource hints. For example, if
Envoy is at EDS version **X** and knows only about cluster ``foo``, but
then receives a CDS update and learns about ``bar`` in addition, it may
issue an additional :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` for **X** with `{foo,bar}` as
`resource_names`.

.. figure:: diagrams/cds-eds-resources.svg
   :alt: CDS response leads to EDS resource hint update

There is a race condition that may arise here; if after a resource hint
update is issued by Envoy at **X**, but before the management server
processes the update it replies with a new version **Y**, the resource
hint update may be interpreted as a rejection of **Y** by presenting an
**X** :ref:`version_info <envoy_api_field_DiscoveryResponse.version_info>`. To avoid this, the management server provides a
``nonce`` that Envoy uses to indicate the specific :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`
each :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` corresponds to:

.. figure:: diagrams/update-race.svg
   :alt: EDS update race motivates nonces

The management server should not send a :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` for any
:ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` that has a stale nonce. A nonce becomes stale
following a newer nonce being presented to Envoy in a
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`. A management server does not need to send an
update until it determines a new version is available. Earlier requests
at a version then also become stale. It may process multiple
:ref:`DiscoveryRequests <envoy_api_msg_DiscoveryRequest>` at a version until a new version is ready.

.. figure:: diagrams/stale-requests.svg
   :alt: Requests become stale

An implication of the above resource update sequencing is that Envoy
does not expect a :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` for every :ref:`DiscoveryRequests <envoy_api_msg_DiscoveryRequest>`
it issues.

.. _xds_protocol_resource_warming:

Resource warming
~~~~~~~~~~~~~~~~

:ref:`Clusters <arch_overview_cluster_warming>` and
:ref:`Listeners <config_listeners_lds>`
go through warming before they can serve requests. This process
happens both during :ref:`Envoy initialization <arch_overview_initialization>`
and when the `Cluster` or `Listener` is updated. Warming of
`Cluster` is completed only when a `ClusterLoadAssignment` response
is supplied by management server. Similarly, warming of `Listener` is
completed only when a `RouteConfiguration` is supplied by management
server if the listener refers to an RDS configuration. Management server
is expected to provide the EDS/RDS updates during warming. If management
server does not provide EDS/RDS responses, Envoy will not initialize
itself during the initialization phase and the updates sent via CDS/LDS
will not take effect until EDS/RDS responses are supplied.

.. _xds_protocol_eventual_consistency_considerations:

Eventual consistency considerations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since Envoy's xDS APIs are eventually consistent, traffic may drop
briefly during updates. For example, if only cluster **X** is known via
CDS/EDS, a `RouteConfiguration` references cluster **X** and is then
adjusted to cluster **Y** just before the CDS/EDS update providing
**Y**, traffic will be blackholed until **Y** is known about by the
Envoy instance.

For some applications, a temporary drop of traffic is acceptable,
retries at the client or by other Envoy sidecars will hide this drop.
For other scenarios where drop can't be tolerated, traffic drop could
have been avoided by providing a CDS/EDS update with both **X** and
**Y**, then the RDS update repointing from **X** to **Y** and then a
CDS/EDS update dropping **X**.

In general, to avoid traffic drop, sequencing of updates should follow a
make before break model, wherein:

- CDS updates (if any) must always be pushed first. 
- EDS updates (if any) must arrive after CDS updates for the respective clusters. 
- LDS updates must arrive after corresponding CDS/EDS updates. 
- RDS updates related to the newly added listeners must arrive after CDS/EDS/LDS updates. 
- VHDS updates (if any) related to the newly added RouteConfigurations must arrive after RDS updates. 
- Stale CDS clusters and related EDS endpoints (ones no longer being referenced) can then be removed.

xDS updates can be pushed independently if no new
clusters/routes/listeners are added or if it's acceptable to temporarily
drop traffic during updates. Note that in case of LDS updates, the
listeners will be warmed before they receive traffic, i.e. the dependent
routes are fetched through RDS if configured. Clusters are warmed when
adding/removing/updating clusters. On the other hand, routes are not
warmed, i.e., the management plane must ensure that clusters referenced
by a route are in place, before pushing the updates for a route.

.. _xds_protocol_ads:

Aggregated Discovery Service
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It's challenging to provide the above guarantees on sequencing to avoid
traffic drop when management servers are distributed. ADS allow a single
management server, via a single gRPC stream, to deliver all API updates.
This provides the ability to carefully sequence updates to avoid traffic
drop. With ADS, a single stream is used with multiple independent
:ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>`/:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` sequences multiplexed via the
type URL. For any given type URL, the above sequencing of
:ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` and :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` messages applies. An
example update sequence might look like:

.. figure:: diagrams/ads.svg
   :alt: EDS/CDS multiplexed on an ADS stream

A single ADS stream is available per Envoy instance.

An example minimal ``bootstrap.yaml`` fragment for ADS configuration is:

.. code:: yaml

    node:
      id: <node identifier>
    dynamic_resources:
      cds_config: {ads: {}}
      lds_config: {ads: {}}
      ads_config:
        api_type: GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: ads_cluster
    static_resources:
      clusters:
      - name: ads_cluster
        connect_timeout: { seconds: 5 }
        type: STATIC
        hosts:
        - socket_address:
            address: <ADS management server IP address>
            port_value: <ADS management server port>
        lb_policy: ROUND_ROBIN
        http2_protocol_options: {}
        upstream_connection_options:
          # configure a TCP keep-alive to detect and reconnect to the admin
          # server in the event of a TCP socket disconnection
          tcp_keepalive:
            ...
    admin:
      ...

Incremental xDS
~~~~~~~~~~~~~~~

Incremental xDS is a separate xDS endpoint that:

-  Allows the protocol to communicate on the wire in terms of
   resource/resource name deltas ("Delta xDS"). This supports the goal
   of scalability of xDS resources. Rather than deliver all 100k
   clusters when a single cluster is modified, the management server
   only needs to deliver the single cluster that changed.
-  Allows the Envoy to on-demand / lazily request additional resources.
   For example, requesting a cluster only when a request for that
   cluster arrives.

An Incremental xDS session is always in the context of a gRPC
bidirectional stream. This allows the xDS server to keep track of the
state of xDS clients connected to it. There is no REST version of
Incremental xDS yet.

In the delta xDS wire protocol, the nonce field is required and used to
pair a :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
to a :ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
ACK or NACK. Optionally, a response message level :ref:`system_version_info <envoy_api_field_DeltaDiscoveryResponse.system_version_info>`
is present for debugging purposes only.

:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>` can be sent in the following situations: 

- Initial message in a xDS bidirectional gRPC stream. 
- As an ACK or NACK response to a previous :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`. In this case the :ref:`response_nonce <envoy_api_field_DiscoveryRequest.response_nonce>` is set to the nonce value in the Response. ACK or NACK is determined by the absence or presence of :ref:`error_detail <envoy_api_field_DiscoveryRequest.error_detail>`. 
- Spontaneous :ref:`DeltaDiscoveryRequests <envoy_api_msg_DeltaDiscoveryRequest>` from the client. This can be done to dynamically add or remove elements from the tracked :ref:`resource_names <envoy_api_field_DiscoveryRequest.resource_names>` set. In this case :ref:`response_nonce <envoy_api_field_DiscoveryRequest.response_nonce>` must be omitted.

In this first example the client connects and receives a first update
that it ACKs. The second update fails and the client NACKs the update.
Later the xDS client spontaneously requests the "wc" resource.

.. figure:: diagrams/incremental.svg
   :alt: Incremental session example

On reconnect the Incremental xDS client may tell the server of its known
resources to avoid resending them over the network. Because no state is
assumed to be preserved from the previous stream, the reconnecting
client must provide the server with all resource names it is interested
in.

.. figure:: diagrams/incremental-reconnect.svg
   :alt: Incremental reconnect example

Resource names
^^^^^^^^^^^^^^

Resources are identified by a resource name or an alias. Aliases of a
resource, if present, can be identified by the alias field in the
resource of a :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`. The resource name will be
returned in the name field in the resource of a
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`.

Subscribing to Resources
^^^^^^^^^^^^^^^^^^^^^^^^

The client can send either an alias or the name of a resource in the
:ref:`resource_names_subscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_subscribe>` field of a :ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>` in
order to subscribe to a resource. Both the names and aliases of
resources should be checked in order to determine whether the entity in
question has been subscribed to.

A :ref:`resource_names_subscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_subscribe>` field may contain resource names that the
server believes the client is already subscribed to, and furthermore has
the most recent versions of. However, the server *must* still provide
those resources in the response; due to implementation details hidden
from the server, the client may have "forgotten" those resources despite
apparently remaining subscribed.

.. _xds_protocol_unsubscribe:

Unsubscribing from Resources
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When a client loses interest in some resources, it will indicate that
with the :ref:`resource_names_unsubscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_unsubscribe>` field of a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`. As with :ref:`resource_names_subscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_subscribe>`, these
may be resource names or aliases.

A :ref:`resource_names_unsubscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_unsubscribe>` field may contain superfluous resource
names, which the server thought the client was already not subscribed
to. The server must cleanly process such a request; it can simply ignore
these phantom unsubscriptions.

REST-JSON polling subscriptions
-------------------------------

Synchronous (long) polling via REST endpoints is also available for the
xDS singleton APIs. The above sequencing of messages is similar, except
no persistent stream is maintained to the management server. It is
expected that there is only a single outstanding request at any point in
time, and as a result the response nonce is optional in REST-JSON. The
`JSON canonical transform of
proto3 <https://developers.google.com/protocol-buffers/docs/proto3#json>`__
is used to encode :ref:`DiscoveryRequest <envoy_api_msg_DiscoveryRequest>` and :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>`
messages. ADS is not available for REST-JSON polling.

When the poll period is set to a small value, with the intention of long
polling, then there is also a requirement to avoid sending a
:ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` unless a change to the underlying resources has
occurred via a :ref:`resource update <xds_protocol_resource_update>`.

.. |Multiple EDS requests on the same stream| image:: diagrams/eds-same-stream.svg
.. |Multiple EDS requests on distinct streams| image:: diagrams/eds-distinct-stream.svg
