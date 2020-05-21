.. _life_of_a_request:

Life of a Request
=================

Below we follow the events that take place after the arrival of a request at the Envoy proxy from a
downstream client, the dispatch to an upstream backend and the corresponding response path.

Network topology
----------------

Envoy can be used in a wide variety of networking topologies. We focus on the inner operation
of Envoy below, but briefly we address how Envoy relates to the rest of the network in this section.

Envoy originated as a `service mesh
<https://blog.envoyproxy.io/service-mesh-data-plane-vs-control-plane-2774e720f7fc>`_ sidecar proxy,
factoring out load balancing, routing, observability, security and discovery services from
applications. In the service mesh model:

* Ingress listeners take requests from other nodes in the service mesh and forward them to the
  local application. Responses from the local application flow back through Envoy to the other
  nodes in the service mesh.
* Egress listeners take requests from the local application and forward them to other nodes in the
  network. These nodes will also be typically running Envoy and accepting the request via their
  ingress listeners.

.. note::
  TODO: add a service mesh diagram

Envoy is used in a variety of domains beyond the service mesh. For example, it can also act
as an internal load balancer and as an edge networking ingress/egress proxy.

.. note::
  TODO: add topology diagrams for edge and ILB.

Envoy may be configured in multi-tier topologies for scalability and reliability:

.. note::
  TODO: add topology diagrams for multi-tier.

In all the above cases, a request will arrive at a specific Envoy via TCP, UDP or Unix Domain
Sockets from a downstream. Envoy will forward requests to upstreams via TCP or Unix Domain
Sockets. We focus on a single Envoy proxy that is part of one of these network topologies
below.

Configuration
-------------

Envoy is a platform with a high degreee of extensibility. This results in a combinatorial
explosion of possible request paths, depending on:

* L3/4 protocol, e.g. TCP, UDP, Unix Domain Sockets.
* L7 protocol, e.g. HTTP/1, HTTP/2, HTTP/3, gRPC, Thrift, Dubbo, Kafka, Redis and various databases.
* Transport socket, e.g. plain text, TLS, ALTS.
* Routing, e.g. PROXY protocol, original destination, dynamic forwarding.
* Authentication and authorization.
* Circuit breakers and outlier detection.
* Many other networking, HTTP, listener, access logging, health checking, tracing and stats
  extensions and configuration options.

It's helpful to limit the nature of the configuration and request to provide an illustrative
narrative below, so we opt for the following parameters:

* An HTTP/2 request with TLS over a TCP connection for both downstream and upstream.
* The HTTP connection manager as the only network filter.
* The router filter as the only HTTP filter (TODO: should we add more filters?).
* Filesystem access logging.
* Statsd sink.

We assume a static bootstrap configuration, but this is a control plane detail that is
orthogonal to the data plane:

.. code-block:: yaml

  todo: simple bootstrap example of above

High level architecture
-----------------------

The request processing path in Envoy has responsibilities split across two major loosely
coupled subsystems:

* *ListenerManager* where the downstream client related aspects of request processing take place.
  This includes all processing from when the first bytes are read from the socket until a
  decoded HTTP request is ready to forward to an upstream cluster. This subsystem is also
  responsible for the response path to the client, the downstream HTTP/2 codec lives here.
* *ClusterManager* where the HTTP request is forwarded to an upstream backend. This is where
  knowledge of cluster and endpoint health, load balancing and connection pooling exists. The
  upstream HTTP/2 codec lives here.

The two subsystems are bridged with the HTTP router filter.

We use the labels *ListenerManager* and *ClusterManager* above to refer to the family of modules and
instance classes that are rooted in these top-level manager classes. In reality, there are numerous
components that we discuss below that are instantiated prior to and during the course of a request
by these management systems, for example listeners, filter chains, codecs, connection pools and load
balancing data structures.

.. note::
  TODO: add architecture diagram.

Request path
------------

Overview
~~~~~~~~

The request path for the initial bytes that constitute the request headers takes the following
steps:

1. A TCP connection from a downstream is accepted by an Envoy listener associated with a worker
   thread.
2. The listener filter chain is instantiated and executed, which can provide SNI and other pre-TLS info.
3. On network reads, the TLS transport socket transforms the encrypted TCP connection
   to a plain text network connection.
4. The network filter chain is instantiated and executed. The most significant filter for HTTP is
   the HTTP connection manager which sits at the end of the filter chain.
5. The HTTP/2 codec in HTTP connection manager lifts the plain text TCP connection to a number of
   independent HTTP streams. Each HTTP stream is a single request.
6. For each HTTP stream, an HTTP filter chain is instantiated and executed. The most significant
   filter for HTTP is the router filter which sits at the end of the filter chain. Once
   *decodeHeaders* processing is completed by the HTTP filter chain, the request headers on the stream
   are ready to start forwarding to the upstream.
7. The router filter requests an HTTP connection pool from the cluster manager for the matched
   cluster.
8. Cluster specific load balancing is performed to find an endpoint. A new connection to the
   endpoint is created if the endpoint's connection pool is empty.
9. The upstream endpoint connection's HTTP/2 codec combines streams from this and other requests into an L4 stream.
10. The upstream endpoint connection's TLS transport socket encrypts these bytes and writes them to
    a TCP socket for the upstream connection.

.. note::
  TODO: high-level request path diagam

Following the request headers, the path setup above processes reads and write events from
the downstream and upstream endpoints on the TCP connection's worker thread. The request
will go through a lifecycle which includes both request and response header processing,
body streaming and trailer processing. Following the completion of a request, post-request
processing will update stats and write to the access log.

The above description is terse, we elaborate on each of these request processing steps below.

Listener TCP accept
~~~~~~~~~~~~~~~~~~~

.. note::
  TODO: elaborate

Listener filter chain processing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

TLS transport socket decryption
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

L4 network filter chain processing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

HTTP/2 codec decoding
~~~~~~~~~~~~~~~~~~~~~

HTTP filter chain processing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Router filter request management
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Load balancing
~~~~~~~~~~~~~~

HTTP/2 codec encoding
~~~~~~~~~~~~~~~~~~~~~

TLS transport socket encryption
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Response path
-------------

.. note::
  TODO: this will also include discussion of how access logging and stats happens on response
  completion. Will write once there is agreement on how to describe the request path.
