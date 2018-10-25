.. _arch_overview_dynamic_config:

Dynamic configuration
=====================

Envoy is architected such that different types of configuration management approaches are possible.
The approach taken in a deployment will be dependent on the needs of the implementor. Simple
deployments are possible with a fully static configuration. More complicated deployments can
incrementally add more complex dynamic configuration, the downside being that the implementor must
provide one or more external REST based configuration provider APIs. This document gives an overview
of the options currently available.

* Top level configuration :ref:`reference <config>`.
* :ref:`Reference configurations <install_ref_configs>`.
* Envoy :ref:`v2 API overview <config_overview_v2>`.

Fully static
------------

In a fully static configuration, the implementor provides a set of :ref:`listeners
<config_listeners>` (and :ref:`filter chains <envoy_api_msg_listener.Filter>`), :ref:`clusters
<config_cluster_manager>`, and optionally :ref:`HTTP route configurations
<envoy_api_msg_RouteConfiguration>`. Dynamic host discovery is only possible via DNS based
:ref:`service discovery <arch_overview_service_discovery>`. Configuration reloads must take place
via the built in :ref:`hot restart <arch_overview_hot_restart>` mechanism.

Though simplistic, fairly complicated deployments can be created using static configurations and
graceful hot restarts.

.. _arch_overview_dynamic_config_eds:

EDS only
------------

The :ref:`Endpoint Discovery Service (EDS) API <envoy_api_file_envoy/api/v2/eds.proto>` provides a
more advanced mechanism by which Envoy can discover members of an upstream cluster.
Layered on top of a static configuration, EDS allows an Envoy deployment to circumvent the
limitations of DNS (maximum records in a response, etc.) as well as consume more information used in
load balancing and routing (e.g., canary status, zone, etc.).

.. _arch_overview_dynamic_config_cds:

EDS and CDS
---------------

The :ref:`cluster discovery service (CDS) API <config_cluster_manager_cds>` layers on a mechanism by
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

EDS, CDS, and RDS
---------------------

The :ref:`route discovery service (RDS) API <config_http_conn_man_rds>` layers on a mechanism by
which Envoy can discover the entire route configuration for an HTTP connection manager filter at
runtime. The route configuration will be gracefully swapped in without affecting existing requests.
This API, when used alongside EDS and CDS, allows implementors to build a complex routing topology
(:ref:`traffic shifting <config_http_conn_man_route_table_traffic_splitting>`, blue/green
deployment, etc).

.. _arch_overview_dynamic_config_lds:

EDS, CDS, RDS, and LDS
--------------------------

The :ref:`listener discovery service (LDS) <config_overview_lds>` layers on a mechanism by which
Envoy can discover entire listeners at runtime. This includes all filter stacks, up to and including
HTTP filters with embedded references to :ref:`RDS <config_http_conn_man_rds>`. Adding LDS into
the mix allows almost every aspect of Envoy to be dynamically configured. Hot restart should
only be required for very rare configuration changes (admin, tracing driver, etc.), certificate
rotation, or binary updates.

EDS, CDS, RDS, LDS, and SDS
-----------------------------

The :ref:`secret discovery service (SDS) <config_secret_discovery_service>` layers on a mechanism
by which Envoy can discover cryptographic secrets (certificate plus private key, TLS session
ticket keys) for its listeners, as well as configuration of peer certificate validation logic
(trusted root certs, revocations, etc).
