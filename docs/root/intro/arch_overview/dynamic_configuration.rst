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
<config_listeners>` (and :ref:`filter chains <config_listener_network_filters>`), :ref:`clusters
<config_cluster_manager>`, and optionally :ref:`HTTP route configurations
<config_http_conn_man_route_table>`. Dynamic host discovery is only possible via DNS based
:ref:`service discovery <arch_overview_service_discovery>`. Configuration reloads must take place
via the built in :ref:`hot restart <arch_overview_hot_restart>` mechanism.

Though simplistic, fairly complicated deployments can be created using static configurations and
graceful hot restarts.

.. _arch_overview_dynamic_config_sds:

SDS/EDS only
------------

The :ref:`service discovery service (SDS) API <config_cluster_manager_sds>` provides a more advanced
mechanism by which Envoy can discover members of an upstream cluster. SDS has been renamed to :ref:`Endpoint
Discovery Service (EDS)<envoy_api_file_envoy/api/v2/eds.proto>` in the
:ref:`v2 API <config_overview_v2>`. Layered on top of a static
configuration, SDS allows an Envoy deployment to circumvent the limitations of DNS (maximum records
in a response, etc.) as well as consume more information used in load balancing and routing (e.g.,
canary status, zone, etc.).

.. _arch_overview_dynamic_config_cds:

SDS/EDS and CDS
---------------

The :ref:`cluster discovery service (CDS) API <config_cluster_manager_cds>` layers on a mechanism by
which Envoy can discover upstream clusters used during routing. Envoy will gracefully add, update,
and remove clusters as specified by the API. This API allows implementors to build a topology in
which Envoy does not need to be aware of all upstream clusters at initial configuration time.
Typically, when doing HTTP routing along with CDS (but without route discovery service),
implementors will make use of the router's ability to forward requests to a cluster specified in an
:ref:`HTTP request header <config_http_conn_man_route_table_route_cluster_header>`.

Although it is possible to use CDS without SDS/EDS by specifying fully static clusters, we recommend
still using the SDS/EDS API for clusters specified via CDS. Internally, when a cluster definition is
updated, the operation is graceful. However, all existing connection pools will be drained and
reconnected. SDS/EDS does not suffer from this limitation. When hosts are added and removed via SDS/EDS,
the existing hosts in the cluster are unaffected.

.. _arch_overview_dynamic_config_rds:

SDS/EDS, CDS, and RDS
---------------------

The :ref:`route discovery service (RDS) API <config_http_conn_man_rds>` layers on a mechanism by which
Envoy can discover the entire route configuration for an HTTP connection manager filter at runtime.
The route configuration will be gracefully swapped in without affecting existing requests. This API,
when used alongside SDS/EDS and CDS, allows implementors to build a complex routing topology
(:ref:`traffic shifting <config_http_conn_man_route_table_traffic_splitting>`, blue/green
deployment, etc.) that will not require any Envoy restarts other than to obtain a new Envoy binary.

.. _arch_overview_dynamic_config_lds:

SDS/EDS, CDS, RDS, and LDS
--------------------------

The :ref:`listener discovery service (LDS) <config_overview_lds>` layers on a mechanism by which
Envoy can discover entire listeners at runtime. This includes all filter stacks, up to and including
HTTP filters with embedded references to :ref:`RDS <config_http_conn_man_rds>`. Adding LDS into
the mix allows almost every aspect of Envoy to be dynamically configured. Hot restart should
only be required for very rare configuration changes (admin, tracing driver, etc.) or binary
updates.
