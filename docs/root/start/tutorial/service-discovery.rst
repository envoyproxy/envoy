.. _service_discovery:

Service discovery
=================

One of the core concepts when setting up Envoy in production is separating the
[data plane](https://blog.envoyproxy.io/service-mesh-data-plane-vs-control-plane-2774e720f7fc)—
the Envoy instances that route your traffic—from the
[control plane](https://blog.envoyproxy.io/service-mesh-data-plane-vs-control-plane-2774e720f7fc),
which acts as the source of truth for the current state of your infrastructure
and your desired configuration.

You can start with a static config file as a control plane, but in most cases,
teams quickly move from a static config file to an API service. Centralizing
configuration in a service makes updates more reliable, taking advantage of
Envoy’s ability to query for new configuration and reload it on the fly. The
first step in setting up your control plane is connecting your service
discovery. This is generally broken down into three steps:

1. Decide on a control plane implementation
2. Publish service definitions to Envoy clusters
3. Publish hosts/containers/instances to Envoy endpoints

Control Plane Implementation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Any control plane should implement the
[Envoy v2 xDS APIs](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api).
For the purpose of publishing service discovery data, you’ll need to implement
the Cluster Discovery Service
[(CDS)](https://www.envoyproxy.io/docs/envoy/latest/configuration/cluster_manager/cds.html)
and the Endpoint Discovery Service
[(EDS)](https://www.envoyproxy.io/docs/envoy/latest/api-v2/api/v2/eds.proto).
Here are several options to avoid starting from scratch:

[Rotor](https://github.com/turbinelabs/rotor) is a **fast, lightweight xDS
implementation with service discovery integration** to Kubernetes, Consul, AWS,
and more. It provides a set of defaults for routing and listeners (RDS and
LDS). It’s also part of the commercial solution
[Houston, by Turbine Labs](https://turbinelabs.io), which adds more
configuration around routing, resilience, and metrics.

The Envoy repository provides
[go-control-plane](https://github.com/envoyproxy/go-control-plane), an
**open-source stub implementation**. If you want to get your hands dirty with
exactly how everything is pulled from service discovery, cached locally, and
served, this is a great starting point.

If you’re running in Kubernetes, the
[Istio project](https://istio.io/docs/concepts/traffic-management/pilot.html)
has a **control plane implementation called Pilot**. It takes YAML files and
turns them into xDS responses. Don’t be scared by the scope of Istio — Pilot
can be used separately to configure Envoy, without pulling in all the other
services like Mixer.

Publish Services to CDS
~~~~~~~~~~~~~~~~~~~~~~~

In Envoy’s vernacular, a “cluster” is a named group of hosts/ports, over which
it will load balance traffic. You may call clusters services, microservices, or
APIs. Envoy will periodically poll the CDS endpoint for cluster configuration,
and expects a response like:

.. code-block:: yaml

   version_info: "0"
   resources:
   - "@type": type.googleapis.com/envoy.api.v2.Cluster
     name: some_service
     connect_timeout: 1.0s
     lb_policy: ROUND_ROBIN
     type: EDS
     eds_cluster_config:
       eds_config:
         api_config_source:
	   api_type: GRPC
           cluster_names: [xds_cluster]

Each service collected by your service discovery maps to one item under
"resources." There are a few additional bits of information that are specific
to load balancing in Envoy that you’ll need to set:

``lb_policy`` sets the type of load balancing. See what Envoy supports in the
docs.

``connect_timeout`` sets the connection timeout. Lower is better: start with 1
second, and raise it if you’re getting flapping due to network latency.

``api_type`` sets the protocol for the service, which your service discovery may
not store directly. Envoy needs to know this to check connections.

It’s possible to hardcode the list of endpoints, though if your infrastructure
is dynamic, you’ll want to set “type” to EDS, which tells Envoy to poll the EDS
API for a list of available IP/ports.

For the full specifications, see the
[Envoy docs](https://www.envoyproxy.io/docs/envoy/latest/api-v1/cluster_manager/cluster.html).
Once this is configured, you can populate the endpoints that serve traffic for
this cluster.

Publish instances to EDS
~~~~~~~~~~~~~~~~~~~~~~~~

Envoy defines an “endpoint” as an IP and port available within a cluster. In
order to balance traffic across a service, Envoy expects the API to provide a
list of endpoints for each service. Envoy periodically polls the EDS endpoint,
generating a response:

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

This is simpler than defining clusters, because the only thing Envoy needs to
know is which cluster(s) this endpoint belongs to.

Envoy treats CDS/EDS service discovery as advisory and eventually consistent;
if traffic to an endpoint fails too often, the endpoint is removed from the
load balancer until healthy again. There’s no need to aggressively remove
endpoints from clusters if they’re unhealthy. **Envoy does that for you!**

Best Practice: Partition your Configs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you have a lot of services, you’ll find that the responses from CDS and EDS
are fairly overwhelming. Envoy can handle them, but if something goes wrong,
making sense of a 5,000-line API response can be quite challenging. The
industry standard is to partition your configs in two ways:

Partition by datacenter / region.
*********************************

In general, services in one datacenter
don’t need to know about the exact endpoint available in other datacenters. To
set up a trickle of traffic between regions (“backhaul,” making the service
robust to region-specific failures), add the remote datacenter’s front proxy to
the local load balancer.

Partition by service need.
**************************

While generally not feasible for an initial
roll-out, the most sophisticated Envoy deployments limit intra-service
communication by only configuring Envoy sidecars to talk to a whitelist of
services. This helps manage the complexity of having 1,000 microservices talk
to each other at any time. It also provides some security protection by
preventing services from making unexpected calls.

In general, partitioning configuration makes it easier to operate both the
Envoy fleet and individual services, at the expense of making the control plane
more complex. Since the control plane isn’t in the critical path of customer
requests, this tends to be a net win for overall system resilience. Many
organizations have reported process wins by making the routing configs
(discussed in the next section) partitioned and self-service, as well.

Next Steps: Set up Routing
~~~~~~~~~~~~~~~~~~~~~~~~~~

Once your control plane knows about all the available services, it’s time to
configure the routes on top of those services. Learn how to set up the Route
Discovery Service
[here](https://www.envoyproxy.io/docs/envoy/latest/configuration/http_conn_man/rds.html#config-http-conn-man-rds).
