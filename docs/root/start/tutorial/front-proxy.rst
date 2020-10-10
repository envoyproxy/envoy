.. _front_proxy:

Front proxy
===========

Front Proxy Deployment
~~~~~~~~~~~~~~~~~~~~~~

One of the most powerful ways to deploy Envoy is as a front proxy. In this
configuration, Envoy acts as the primary load balancer for customer requests
from the public internet. Managing this public traffic—also called north-south
traffic—is something that every web app must do, so it’s a natural place to
start with Envoy. This kind of deployment style is also called an edge proxy or
simply a load balancer.

Starting with a front proxy is straightforward, because the first step is
simply to port whatever existing configuration you have to Envoy. When you
deploy your Envoy-based front proxy, you can set it up in parallel to your
current setup, giving you the change to test and evaluate Envoy before sending
production traffic through it. Once Envoy is stable and tested, it provides a
high-leverage place to start enabling Envoy’s more advanced features around
resilience and observability for your whole organization.

Deploying a Front Proxy
~~~~~~~~~~~~~~~~~~~~~~~

Though Envoy is capable enough to be deployed right at the edge of your
network, most public cloud providers expose layer 3/4 load balancers with more
capabilities than you need. The typical deployment will use
`AWS’ NLB <https://docs.aws.amazon.com/elasticloadbalancing/latest/network/introduction.html>`_,
`GCP’s Regional NLB <https://cloud.google.com/compute/docs/load-balancing/network/>`_,
or a minimal configuration of a more feature-rich load balancer
like AWS ALB. These baked-in load balancers can handle regional or global
traffic balancing, which is impossible for a set of VMs running Envoy (which
are necessarily constrained to a single availability zone) to handle.

A simple, robust deployment of an Envoy front proxy uses an autoscaling group
of Envoys based on network traffic. Based on where your routing rules currently
live (NGINX config files, AWS ALB configuration, etc.), you will need to port
these to a set of Envoy routes. See :ref:`Routing configuration <routing_configuration>`
for more details.

Most modern apps will want to implement a dynamic control plane, since the
instances within each service are dynamic. A static configuration can only
point to a fixed set of instances, while a dynamic control plane can keep Envoy
up-to-date on the state of your environment, typically by reading from your
service discovery registry. See :ref:`Integrating service discovery with Envoy <routing_configuration>`
for implementations that will do this for you, like
`Rotor <https://github.com/turbinelabs/rotor>`_.

For an example of how this would work in AWS,
`see this repository, which uses AWS, CloudFormation, and Rotor <https://github.com/turbinelabs/examples/tree/master/rotor-nlb>`_.

If you’re looking to deploy Envoy for internal traffic only, see
:ref:`Basic service mesh <service_mesh>`

Deploying Envoy in Kubernetes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you’re in Kubernetes, you can point NLBs directly to a an exposed Kubernetes
service in front of an Envoy deployment.

Here’s what that Deployment might look like. It uses
`envoy-simple <https://github.com/turbinelabs/envoy-simple>`_, a Docker
container that allows Envoy to be fully dynamically configured by specifying a
control plane implementation via environment variables. Simply change
``ENVOY_XDS_HOST`` to the network location of your control plane (typically
another Kubernetes service).

.. literalinclude:: _include/kubernetes-xds.yaml
    :language: yaml

Assuming you’ve saved this file as ``envoy.yaml``, you can create a load balancer
in your cloud provider with ``kubectl``.

.. code-block:: console

   $ kubectl create -f envoy.yaml
   $ kubectl expose deployment --type=LoadBalancer --port=80 envoy-front-proxy

You can also use an ingress controller like
`Contour <https://projectcontour.io>`_ if you want to manage everything
through Kubernetes. These expose Envoy’s configuration as
`Kubernetes Ingress Resources <https://kubernetes.io/docs/concepts/services-networking/ingress/>`_.
This is simple, but less expressive than configuring Envoy through a
general-purpose control plane, as the Kubernetes ingress controller spec is
lowest-common-denominator by design, with support for only a subset of Envoy’s
capabilities. There are also Kubernetes-native API Gateways like
`Ambassador <https://github.com/datawire/ambassador>`_ that offer expanded
functionality by using other, more expressive Kubernetes resources for
configuration.

Note that while running Envoy in Kubernetes is simpler if you’re committed to
Kubernetes, many people start by running Envoy outside of Kubernetes in order
to manage the migration. Running Envoy outside of Kubernetes ensures that any
cluster failures doesn’t take down your Envoy front proxy. If you leave Envoy
running outside the cluster after completing you migration, it also gives you
flexibility to keep running other orchestrators, or even migrate to the thing
that replaces Kubernetes!

SSL and Metrics
~~~~~~~~~~~~~~~

Front proxies are a natural place to terminate SSL, to ensure that individual
services don’t have to. You can either do in your cloud’s load balancer (e.g.
AWS ELB) or in Envoy itself. To configure SSL, see :ref:`this article <ssl>`

As a common choke point for all traffic, front proxies are a great place to
generate high-level metrics for your site. Make sure to at least send request
health metrics to your dashboards: latency, volume, and success rate. Coarsely,
this gives you a simple dashboard for “is the site up?” and partitioning the
traffic by upstream cluster can give you health metrics for each service that
are more relevant to the service teams.

If you generate logs from your current front proxy, Envoy can be configured to
send request-level access logs to a centralized server. Be aware that the
volume of traffic at the edge can make logging expensive or slow, so this may
not be cost-effective if it’s not a critical capability.

Similarly, Envoy provides drop-in configuration for tracing, and the front
proxy is a great place to generate those tracing headers.

Multiple Data Centers Made Simple
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are two strategies for running services in multiple zones (availability
zones, regions, data centers, etc.). You can pick a single strategy, or you can
run both hierarchically.

If you have a *single network space*, your best bet is to use
**Envoy’s Zone Aware Routing** to handle any underlying discrepancies in the
topology seamlessly. This strategy is best used with closely connected zones
like AWS Availability Zones, where multiple zones are operationally a single
unit, but cross-zone traffic incurs a cost. In this setup, your network load
balancer would balance across a front proxy that spans multiple zones, and
Envoy would keep each request in the zone it was first load balanced to. This
will minimize cross-zone traffic (which is expensive and slow), while
maintaining the ability to incrementally fall back to out-of-zone instances if
the local ones are failing.

Setting up zone-aware load balancing requires two options to be set:

  - Each cluster (statically defined or returned by the control plane via CDS)
    must enable zone-aware routing.
  - Each Envoy must set ``local_cluster_name`` to one of the named clusters.

Statically defined, this looks like:

.. code-block:: yaml

   local_cluster_name: service1
   clusters:
   - name: service1
     zone_aware_lb_config:
       min_cluster_size: 3

On the other hand, *fully isolated zones* should be configured with
**entirely separate service discovery integrations**. Instead of giving each
Envoy the full routing table with all instances, it’s better to only enable
each Envoy to route to instances in its own zone. To enable these zones to fail
over gracefully, you can add remote front proxies to each cluster with a low
weight.

This “backhaul” path will allow automatic failover while allowing zones to
maintain different internal configurations. For example, if you are doing a
release in the EU region, having the US region fail to the EU’s front proxy
keeps the US region from inadvertently routing too much traffic to the new
version if there’s a failure mid-release.

Best Practices
~~~~~~~~~~~~~~

Once you have a front proxy up and running, there are several ways to make
operating it easier for you, your team, and the rest of the company.

Make routing self-serve.
************************

Microservices mean a lot more changes, and service
teams should be part of the process. Add guardrails (either people process or
RBAC-style rules) to prevent things from breaking.

Centralize authentication at the edge.
**************************************

Adding a single authentication call at  the edge prevents every service from
re-verifying the user each time. Take the cookie from the user, verify it with
the authentication service, and store the result (including any commonly used
data like a user ID) in a header that gets passed to internal services and
trusted without further verification.

Add protections from bad actors and unexpected traffic spikes.
**************************************************************

This means :ref:`automatic retries <automatic_retries>`,
:ref:`health checks <health_check>`, etc. What this looks like depends strongly
on your infrastructure and the types of issues you’re looking to mitigate.

Next Steps
~~~~~~~~~~

While this article has focused on how to handle traffic coming from outside
your network, it's also possible for Envoy to handle traffic between services
(or "east-west" traffic). For a lightweight way to set up this internal mesh,
you can route internal requests through this front proxy (or a similarly
configured proxy pool specifically for east-west). Beyond that, you can take
better advantage of Envoy’s unique features as a lightweight sidecar by
:ref:`setting up a basic service mesh <service_mesh>`.
