.. _config_cluster_manager_cds:

Cluster discovery service
=========================

The cluster discovery service (CDS) is an optional API that Envoy will call to dynamically fetch
cluster manager members. Envoy will reconcile the API response and add, modify, or remove known
clusters depending on what is required.

.. note::

  Any clusters that are statically defined within the Envoy configuration cannot be modified or
  removed via the CDS API.

* :ref:`v2 CDS API <v2_grpc_streaming_endpoints>`

Statistics
----------

CDS has a :ref:`statistics <subscription_statistics>` tree rooted at *cluster_manager.cds.*

On-demand CDS
-------------

Similar to VHDS on-demand feature in terms of hosts, the on-demand CDS API is an additional API
that Envoy will call to dynamically fetch upstream clusters which Envoy interested in spontaneously.

By default in CDS, all cluster configurations are sent to every Envoy instance in the mesh. The
delta CDS provides the ability that the xDS management server can send incremental CDS to the Envoy
instance, but Envoy instance can not feedback upstream clusters it interested in spontaneously, in
other words, Envoy instance only can receive the CDS passively.

In order to fix this issue, on-demand CDS uses the delta xDS protocol to allow a cluster configuration
to be subscribed to and the necessary cluster configuration to be requested as needed. Instead
of sending all cluster configuration or cluster configuration the Envoy instance aren't interested
in, using on-demand CDS will allow an Envoy instance to subscribe and unsubscribe from a list of
cluster configurations stored internally in the xDS management server. The xDS management server
will monitor the list and use it to filter the configuration sent to an individual Envoy instance
to only contain the subscribed cluster configurations.

Subscribing to resources
^^^^^^^^^^^^^^^^^^^^^^^^

On-demand CDS allows resources to be :ref:`subscribed <xds_protocol_delta_subscribe>` to using
a :ref:`DeltaDiscoveryRequest <envoy_api_msg_DeltaDiscoveryRequest>`
with the :ref:`type_url <envoy_api_field_DeltaDiscoveryRequest.type_url>` set to
`type.googleapis.com/envoy.api.v2.Cluster` and
:ref:`resource_names_subscribe <envoy_api_field_DeltaDiscoveryRequest.resource_names_subscribe>`
set to a list of cluster resource names for which it would like configuration.

Typical use case
^^^^^^^^^^^^^^^^

Sometimes, there will be a large amount of broker instances provided for
`Apache RocketMQ <http://rocketmq.apache.org/>`_ to produce/consume messages. Perhaps the size of
SToW CDS configurations will be more than 1GB, so it is not practical to deliver the SToW CDS from the
management server every time, which will cause huge overhead. In this case, on-demand CDS is essential.