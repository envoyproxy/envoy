.. _arch_overview_dynamic_config:

xDS configuration API overview
==============================

Envoy is architected such that different types of configuration management approaches are possible.
The approach taken in a deployment will be dependent on the needs of the implementor. Simple
deployments are possible with a fully static configuration. More complicated deployments can
incrementally add more complex dynamic configuration, the downside being that the implementor must
provide one or more external gRPC/REST based configuration provider APIs. These APIs are
collectively known as :ref:`"xDS" <xds_protocol>` (* discovery service). This document gives an
overview of the options currently available.

* Top level configuration :ref:`reference <config>`.
* :ref:`Reference configurations <install_ref_configs>`.
* Envoy :ref:`v2 API overview <config_overview>`.
* :ref:`xDS API endpoints <config_overview_management_server>`.

Fully static
------------

In a fully static configuration, the implementor provides a set of :ref:`listeners
<config_listeners>` (and :ref:`filter chains <envoy_api_msg_listener.Filter>`), :ref:`clusters
<config_cluster_manager>`, etc. Dynamic host discovery is only possible via DNS based
:ref:`service discovery <arch_overview_service_discovery>`. Configuration reloads must take place
via the built in :ref:`hot restart <arch_overview_hot_restart>` mechanism.

Though simplistic, fairly complicated deployments can be created using static configurations and
graceful hot restarts.

.. _arch_overview_dynamic_config_eds:

EDS
---

The :ref:`Endpoint Discovery Service (EDS) API <arch_overview_service_discovery_types_eds>` provides
a more advanced mechanism by which Envoy can discover members of an upstream cluster. Layered on top
of a static configuration, EDS allows an Envoy deployment to circumvent the limitations of DNS
(maximum records in a response, etc.) as well as consume more information used in load balancing and
routing (e.g., canary status, zone, etc.).

.. _arch_overview_dynamic_config_cds:

CDS
---

The :ref:`Cluster Discovery Service (CDS) API <config_cluster_manager_cds>` layers on a mechanism by
which Envoy can discover upstream clusters used during routing. Envoy will gracefully add, update,
and remove clusters as specified by the API. This API allows implementors to build a topology in
which Envoy does not need to be aware of all upstream clusters at initial configuration time.
Typically, when doing HTTP routing along with CDS (but without route discovery service),
implementors will make use of the router's ability to forward requests to a cluster specified in an
:ref:`HTTP request header <envoy_api_field_route.RouteAction.cluster_header>`.

Although it is possible to use CDS without EDS by specifying fully static clusters, we recommend
still using the EDS API for clusters specified via CDS. Internally, when a cluster definition is
updated, the operation is graceful. However, all existing connection pools will be drained and
reconnected. EDS does not suffer from this limitation. When hosts are added and removed via EDS, the
existing hosts in the cluster are unaffected.

.. _arch_overview_dynamic_config_rds:

RDS
---

The :ref:`Route Discovery Service (RDS) API <config_http_conn_man_rds>` layers on a mechanism by
which Envoy can discover the entire route configuration for an HTTP connection manager filter at
runtime. The route configuration will be gracefully swapped in without affecting existing requests.
This API, when used alongside EDS and CDS, allows implementors to build a complex routing topology
(:ref:`traffic shifting <config_http_conn_man_route_table_traffic_splitting>`, blue/green
deployment, etc).

VHDS
----
The :ref:`Virtual Host Discovery Service <config_http_conn_man_vhds>` allows the virtual hosts belonging
to a route configuration to be requested as needed separately from the route configuration itself. This
API is typically used in deployments in which there are a large number of virtual hosts in a route
configuration.

SRDS
----

The :ref:`Scoped Route Discovery Service (SRDS) API <arch_overview_http_routing_route_scope>` allows
a route table to be broken up into multiple pieces. This API is typically used in deployments of
HTTP routing with massive route tables in which simple linear searches are not feasible.

.. _arch_overview_dynamic_config_lds:

LDS
---

The :ref:`Listener Discovery Service (LDS) API <config_listeners_lds>` layers on a mechanism by which
Envoy can discover entire listeners at runtime. This includes all filter stacks, up to and including
HTTP filters with embedded references to :ref:`RDS <config_http_conn_man_rds>`. Adding LDS into
the mix allows almost every aspect of Envoy to be dynamically configured. Hot restart should
only be required for very rare configuration changes (admin, tracing driver, etc.), certificate
rotation, or binary updates.

SDS
---

The :ref:`Secret Discovery Service (SDS) API <config_secret_discovery_service>` layers on a mechanism
by which Envoy can discover cryptographic secrets (certificate plus private key, TLS session
ticket keys) for its listeners, as well as configuration of peer certificate validation logic
(trusted root certs, revocations, etc).

RTDS
----

The :ref:`RunTime Discovery Service (RTDS) API <config_runtime_rtds>` allows
:ref:`runtime <config_runtime>` layers to be fetched via an xDS API. This may be favorable to,
or augmented by, file system layers.

Aggregated xDS ("ADS")
----------------------

EDS, CDS, etc. are each separate services, with different REST/gRPC service names, e.g.
StreamListeners, StreamSecrets. For users looking to enforce the order in which resources of
different types reach Envoy, there is aggregated xDS, a single gRPC service that carries all
resource types in a single gRPC stream. (ADS is only supported by gRPC).
:ref:`More details about ADS <config_overview_ads>`.

.. _arch_overview_dynamic_config_delta:

Delta gRPC xDS
--------------

Standard xDS is "state-of-the-world": every update must contain every resource, with the absence of
a resource from an update implying that the resource is gone. Envoy supports a "delta" variant of
xDS (including ADS), where updates only contain resources added/changed/removed. Delta xDS is a
new protocol, with request/response APIs different from SotW.
:ref:`More details about delta <config_overview_delta>`.
