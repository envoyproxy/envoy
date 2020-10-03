.. _service_mesh:

Service mesh
============

The recent popularity of microservices has made the need for safe, reliable
service-to-service communication more apparent than ever. Envoy’s lightweight
footprint, powerful routing constructs, and flexible observability support make
it a great proxy to build a service mesh on. In this configuration, Envoy acts
as the primary load balancer for requests between internal services. Managing
this internal traffic — also called “East/West” traffic — is trivial in an
environment with a small number of services, but with tens or hundreds of
services making calls to one another, it’s combinatorially more complex.

That's where the Envoy service mesh comes in. This is a complementary
deployment to a [Front Proxy](front-proxy), where Envoy handles traffic
from the outside world (aka north-south traffic).

A basic Service Mesh uses Envoy sidecars to handle outbound traffic for each
service instance. This allows Envoy to handle load balancing and resilience
strategies for all internal calls, as well as providing a coherent layer for
observability. Services are still exposed to the internal network, and all
network calls pass through an Envoy on ``localhost``. Rolling out a basic Service
Mesh can be done one service at a time, making it a practical first step for
most Envoy deployments.

Envoy as a Sidecar
~~~~~~~~~~~~~~~~~~

Kubernetes
**********

Kubernetes makes adding Envoy sidecars easy. You’ll need to do two things:

  1. Add an Envoy container to your Pod spec
  2. Modify your services to send all outbound traffic to this sidecar

How you want to configure Envoy will vary depending on your environment—more on
that below. If you want to use fully dynamic configuration, you can use a
container like [envoy-simple](https://github.com/turbinelabs/envoy-simple) and
set the location of the
[various](service-discovery)
[configuration services](routing-configuration) with
environment variables.

.. code-block:: yaml

   - image: turbinelabs/envoy-simple:0.17.2
     imagePullPolicy: Always
     name: envoy-simple
     ports:
     - containerPort: 8000
     env:
     - name: ENVOY_XDS_HOST
       value: "rotor.default.svc.cluster.local"
     - name: ENVOY_XDS_PORT
       value: "50000"


Since Pods share the same notion of localhost, you can simply change your
service to have them call localhost on port 8000 with the correct Host header
set, instead of calling the remote service. If you’re using Kubernetes’
Services, you can override the environment variables (e.g.
``SERVICENAME_SERVICE_HOST`` and ``SERVICENAME_SERVICE_PORT``) or Kubernetes’ DNS
with your Envoy’s listener value.

Other Environments
******************

Outside of Kubernetes, you have much more flexibility in how you deploy Envoy.
You can run either the Envoy container or the binary on your hosts. Similar to
Kubernetes, by running Envoy on localhost, you only have to change your
services to communicate with Envoy on the port you specify.

Docker, listening on port 8000:

.. code-block:: console

   $ docker run -d \
        -e 'ENVOY_XDS_HOST=127.0.0.1' \
	-e 'ENVOY_XDS_PORT=50000' \
	-p 8000:8000 \
      turbinelabs/envoy-simple:0.17.2

Next, you should modify your services to route through Envoy. If changing their
configuration or code isn’t possible, you can force all outbound traffic
through Envoy with something like /etc/hosts or iptables.

Sidecar Configuration
~~~~~~~~~~~~~~~~~~~~~

The easiest way to get started is to have Envoy handle traffic the same as
your internal network does. Practically, this means three things:

  - **Expose a single listener for your services to send outbound traffic to.**
    This matches with the port exposed on your container, e.g. 8000 in the example
    configs above. Inbound traffic skips Envoy and continues to talk
    directly to the services. Adding a listener to handle incoming traffic will
    be covered in Advanced Service Mesh (coming soon!).

  - **Serve the full route table in all sidecars**. By exposing all services
    to all other services, you’ll ensure nothing breaks on the first iteration.
    If you have a [Front Proxy](front-proxy), re-using these routes can save
    time. If not, it’s straightforward to create a
    [basic set of routes and listeners](routing-basics)  in a static Envoy
    configuration file. Once that’s working in production, it may make sense to
    limit the routes available for each service. The explicit routing between
    services helps service teams understand where their internal traffic is
    coming from, helping them define mutual SLOs.

  - **Consider using dynamic configuration for instance discovery in the first iteration**. Specifically, using
    [EDS to update Envoy’s notion of available hosts](service-discovery)
    with an EDS server like [Rotor](https://github.com/turbinelabs/rotor) keeps
    Envoy’s routing tables in sync with the underlying infrastructure. Envoy can
    use static configuration for listeners and routes, so it’s simple and
    valuable to set up a control plane to manage instance availability.

If you’ve been following the examples above, you can set up
[Rotor](https://github.com/turbinelabs/rotor), an Envoy control plane and
service discovery bridge, to implement xDS. Remember that Envoy can mix static
and dynamic configuration, so if you want to statically configure listeners,
routes, and clusters (LDS / RDS / CDS), you can use your own Envoy container
with a static config file while still using a dynamic EDS control plane.
Eventually, there are [good reasons](routing-configuration)
to move to a fully dynamic system.

Observability
~~~~~~~~~~~~~

One of the biggest benefits of a service mesh is that it provides a uniform
view over your services. Each service will certainly have metrics and tooling
unique to it, but Envoy provides a simple way to get the same high-level
metrics for all services. Keep the following principles in mind when deciding
which metrics to look at:

  - **Pick metrics that relate to customer experience**. In particular, Envoy
    can generate request volume, request rate, and latency histograms. Resource
    metrics like number of connections or amount of network traffic can mean
    different things on different services. See how
    [Lyft does it here](https://blog.envoyproxy.io/lyfts-envoy-dashboards-5c91738816b1).

  - **Segmentation of simple metrics, not more types of metrics.**
    Envoy can produce a stunning number of metrics. Teams with lots of services
    tend to get more value out of a small set of metrics, segmented by service,
    instance, and region

  - **Add tracing in Envoy.** Since Envoy is present at every network hop, it’s
    guaranteed to capture all intra-instance communication. This means that a
    single configuration can produce complete traces across the entire mesh.
    That’s a powerful framework to add more detailed custom instrumentation.

_Note: you will have to propagate headers through each service to create full
  traces._

Multiple Regions
~~~~~~~~~~~~~~~~

As described in [Front Proxy](front-proxy), you should have one front
proxy per datacenter. When setting up a mesh, it’s generally safer to send
intra-data center traffic to the remote front proxy, instead of exposing all of
the internals to all datacenters. This can simplify incident management as
well, because changes to a single region are less likely to affect other
regions.

This also means you should split up the configs. Generally you can do this by
running a different control plane in each data center. If you want to run a
single control plane, check out the discussion of Locality in Advanced Service
Mesh (coming soon!).

In this setup, you would still map each service to a single Envoy cluster, but
instead of including the remote instances, you’d include the remote front proxy
as the out-of-zone instance in the cluster.

Next Steps
~~~~~~~~~~

While this article has focused on how to handle traffic between services, it's
also possible for Envoy to handle traffic from the public internet
(“North/South” traffic) as a
[Front Proxy](front-proxy). The service mesh and
front proxy have a lot of overlapping features, so it can be useful to consider
how to roll them both out.

Beyond that, you can set up Envoy to also handle incoming traffic on each node
within your service mesh. This gives better isolation between services and
better observability.
