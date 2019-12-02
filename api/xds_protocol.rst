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
consistent. See :ref:`Aggregated Discovery Service <xds_protocol_ads>`
below for situations in which explicit control of sequencing is required.

Type URLs
^^^^^^^^^

Each xDS API is concerned with resources of a given type. There is a 1:1
correspondence between an xDS API and a resource type. That is:

-  LDS: :ref:`envoy.api.v2.Listener <envoy_api_msg_Listener>`
-  RDS: :ref:`envoy.api.v2.RouteConfiguration <envoy_api_msg_RouteConfiguration>`
-  VHDS: :ref:`envoy.api.v2.VirtualHost <envoy_api_msg_route.VirtualHost>`
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

Only the first request on a stream is guaranteed to carry the node identifier.
The subsequent discovery requests on the same stream may carry an empty node
identifier. This holds true regardless of the acceptance of the discovery
responses on the same stream. The node identifier should always be identical if
present more than once on the stream. It is sufficient to only check the first
message for the node identifier as a result.

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

The :ref:`resource_names <envoy_api_field_DiscoveryRequest.resource_names>` specified in the :ref:`DiscoveryRequest
<envoy_api_msg_DiscoveryRequest>` are a hint about what resources the client is interested in.

For :ref:`Listener Discovery Service (LDS) <envoy_api_msg_Listener>` and :ref:`Cluster Discovery Service (CDS)
<envoy_api_msg_Cluster>`, Envoy will always set the resource hints to empty, in which case the server should use
site-specific business logic to determine the full set of resources that the client is interested in, typically based on
the client's node identification. However, other xDS clients may specify explicit LDS/CDS resources as resource hints, for
example if they only have a singleton listener and already know its name from some out-of-band configuration. For EDS/RDS, the
resource hints are required.

When the resource hints are specified, the management server must supply the requested resources if they exist. The client will
silently ignore any supplied resources that were not explicitly requested. When the client sends a new request that changes
the *resource_names* list, the server must resend any newly requested resource, even if it previously sent it without having
been asked for it and the resource has not changed since that time.

For LDS and CDS, it is expected that the management server will provide the complete state of the LDS/CDS resources in each
response; an absent `Listener` or `Cluster` will be deleted. For RDS or EDS, when a requested resource is
missing, Envoy will retain the last known value for this resource except in the case where the `Cluster` or `Listener` is being
warmed. See :ref:`Resource Warming <xds_protocol_resource_warming>` section below on the expectations during warming.
An empty EDS/RDS :ref:`DiscoveryResponse <envoy_api_msg_DiscoveryResponse>` is effectively a nop from the perspective of the
respective resources in the Envoy.

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

.. _xds_protocol_delta:

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

Implementations
---------------

.. _on_demand_vhds_rds_protocol:

Virtual Host Discovery Service
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
By default in RDS, all routes for a cluster are sent to every Envoy instance
in the mesh. This causes scaling issues as the size of the cluster grows. The
majority of this complexity can be found in the virtual host configurations, of
which most are not needed by any individual proxy. 

In order to fix this issue, the Virtual Host Discovery Service (VHDS) protocol
uses the delta xDS protocol to allow a route configuration to be subscribed to
and the necessary virtual hosts to be requested as needed. Instead of sending
allmvirtual hosts with a route config, using VHDS will allow an Envoy instance
to subscribe and unsubscribe from a list of virtual hosts stored internally in
the xDS management server. The xDS management server will monitor this list and
use it to filter the configuration sent to an individual Envoy instance to only
contain the subscribed virtual hosts.

In VHDS, Envoy will send a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
with the 
:ref:`type_url <envoy_api_field_DeltaDiscoveryRequest.type_url>` set to 
`type.googleapis.com/envoy.api.v2.route.VirtualHost` 
and :ref:`resource_names_subscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_subscribe>` 
set to a list of virtual host resource names for which it would like
configuration. The management server will respond with a
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
with the :ref:`resources <envoy_api_field_DeltaDiscoveryResponse.resources>`
field populated with the specified virtual hosts, the 
:ref:`name <envoy_api_field_resource.name>` field populated with
the virtual host name, and the 
:ref:`aliases <envoy_api_field_resource.aliases>` field
populated with the explicit (no wildcard) domains of the virtual host. Future
updates to these virtual hosts will be sent via spontaneous updates.

Updates to the route configuration entry to which a virtual host belongs will
clear the virtual host table and require all virtual hosts to be sent again. It
may be useful for the management server to populate and RDS responses with the
subscribed list of virtual hosts. 

Joining/Reconnecting to a management server
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
When an Envoy instance forms a delta xDS connection to the xDS management
server for the first time, it will receive a base configuration filtered down
to a subset of routes that are likely useful to the Envoy instance. For
example, these might be filtered down by routes that exist in the current
namespace. The xDS management server will send this same collection of
resources on reconnect.

.. figure:: diagrams/delta_rds_connection.svg
   :alt: Delta RDS connection

Virtual Host Naming Convention
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Virtual hosts in VHDS are identified by a combination of the name of the route
configuration to which the virtual host belong as well as the host HTTP "host"
header (authority for HTTP2) entry. Resources should be named as follows:

<route configuration name>/<host entry>

Note that matching should be done from right to left since a host entry cannot
contain slashes while a route configuration name can. 

Requesting Additional resources
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Envoy will send a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>` with the
:ref:`resource_names_subscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_subscribe>`
field populated with the route config names
+ domains of each of the resources that it would like to subscribe to. Each of
the virtual hosts contained in the
:ref:`DeltaDiscoveryRequest's <envoy_api_msg_DeltaDiscoveryRequest>`
resources field will be added to the route configuration maintained by Envoy.
If Envoy's route configuration already contains a given virtual host, it will
be overwritten by data received in the
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
but only if the updated virtual host is different from its current state.
The :ref:`resource.aliases <envoy_api_field_resource.aliases>` field contains
all host/authority header values used to create the on-demand request. During
spontaneous updates configuration server will only send updates for virtual
hosts that Envoy is aware of. The configuration server needs to keep track
of virtual hosts known to Envoy.

If a virtual host is requested for which the management sever does not know
about, then the management server should respond with a 
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>` in which
the :ref:`resources <envoy_api_field_DeltaDiscoveryResponse.resources>` entry
for that virtual host has the :ref:`name <envoy_api_field_resource.name>` and
:ref:`aliases <envoy_api_field_resource.aliases>` set to the requested host
entry and the resource unpopulated. This will allow Envoy to match the
requested resource to the response.

Envoy should respond to the 
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
with an Ack/Nack 
:ref:`DeltaDiscoveryRequest's <envoy_api_msg_DeltaDiscoveryRequest>`.

.. figure:: diagrams/delta_rds_request_additional_resources.svg
   :alt: Delta RDS request additional resources

Request Path Resolution
+++++++++++++++++++++++
If a route for the contents of the host/authority header cannot be resolved:
- A :ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>` as 
described above is queued for transmission.
- A callback resuming the decoder filter chain of the current active stream is
created. Together with the callback the contents of all host/authority headers
used in the request is stored. 
- The decoder filter chain of the current active stream is paused.
If there's already a route available, the control is passed to the next filter
in the filter chain.

When a :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
arrives:
- Route configuration is updated.
- All callbacks whose list of host/authority header values exactly matches the
list in the :ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
are triggered.
- The decoder filter chain is resumed. If a route for the host/authority header
can be found, the active stream is recreated (to pick up the updated route
configuration). If there's still no route, the control is passed to the next
filter in the filter chain.
  
Unsubscribing from Virtual Hosts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The xDS management server will also support the ability of Envoy to tell it
when a resource hasn't been used and is safe to stop monitoring. The resources
that can be removed include the base resources that the xDS management server
initially sent Envoy.

Virtual hosts can be unsubscribed from via a
:ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
with their route config names + domains provided in the
:ref:`resource_names_unsubscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_unsubscribe>`
field. Envoy will remove any route config names + domains that it finds in the
:ref:`DeltaDiscoveryResponse <envoy_api_msg_DeltaDiscoveryResponse>`
:ref:`removed_resources <envoy_api_field_DeltaDiscoveryResponse.removed_resources>`
field.

Compatibility with Scoped RDS
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

VHDS shouldn't present any compatibility issues with 
:ref:`scoped RDS <envoy_api_msg_ScopedRouteConfiguration>`.
Route configuration names can still be used for virtual host matching, but with
scoped RDS configured it would point to a scoped route configuration.

However, it is imporant to note that using
on-demand :ref:`scoped RDS <envoy_api_msg_ScopedRouteConfiguration>`
and VHDS together will require two on-demand subscriptions per routing scope.