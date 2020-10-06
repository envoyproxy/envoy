.. _routing_configuration:


Routing configuration
=====================

When running a large Envoy fleet in production, it’s important to separate the
data plane — user traffic — from the control plane, which includes Envoy
configuration and infrastructure state. Setting up a simple control plane
generally includes choosing configuration options like [automatic
retries](automatic-retries.html) and integrating [service
discovery](service-discovery.html).

One of the biggest advantages of creating a distinct, centralized control plane
is that it provides a source of truth for routing configuration. In legacy
systems, this is stored in a mix of web server configuration files, load
balancer configs, and application-specific routing definitions
(e.g. ``routes.rb``). Centralizing these definitions makes them safe and easy to
change. This provides teams with flexibility during migrations, releases, and
other major system changes.

Serving Routes via RDS
~~~~~~~~~~~~~~~~~~~~~~

Envoy’s dynamic configuration allows these routing configurations to execute
rules defined in a control plane with its Route Discovery Service, or
[RDS](https://www.envoyproxy.io/docs/envoy/latest/configuration/http_conn_man/rds). The
control plane holds a mapping between a domain + path and an Envoy “cluster.”
The control plane serves config definitions via RDS, and the Envoy instances
implement the actual traffic control.

Here is a simple example of a route in RDS:

.. code-block:: yaml

   version_info: "0"
   resources:
   - "@type": type.googleapis.com/envoy.api.v2.RouteConfiguration
     name: local_route
     virtual_hosts:
     - name: local_service
       domains: ["*"]
       routes:
       - match: { prefix: "/" }
	 route: { cluster: some_service }

Both open-source
([go-control-plane](https://github.com/envoyproxy/go-control-plane), [Istio
Pilot](https://istio.io/docs/concepts/traffic-management/pilot.html)) and
commercial ([Houston](http://turbinelabs.io/product)) implementations of RDS are
available, or the Envoy docs define a
[full RDS specification](https://www.envoyproxy.io/docs/envoy/latest/configuration/overview/v2_overview.html#v2-grpc-streaming-endpoints)
for teams that want to roll their own. Keep in mind that the RDS specification
is only the transport mechanism; how you manage the state is up to you,
discussed in more detail below.

Best Practices for Routing Definitions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Because there may be thousands of Envoy instances in a large system, the control
plane should be the source of truth for all routes. Requests can come directly
from users, from internal services, or from different cloud regions. Because of
this, it’s best to deploy Envoy to handle these diverse network topologies
(e.g. as a front proxy for customer traffic plus a service mesh for internal
traffic) and have them all behave similarly, no matter where the traffic comes
from.

In order to scale out a single system for routing definitions, there are three
key principles to follow:

1. Treat routes as data, not config
2. Distribute control to teams with ACLs
3. Design for changes with audit logs and rollbacks

#1: Treat routes as data
************************

Treating routes as data in a shared service **prevents conflicts**, provides the
right starting point for **managing convergence times**, and **ensures
semantically correct definitions**. While tools like Istio make it easy to write
YAML-based routes, managing hundreds of routes across thousands of lines of YAML
makes it difficult to prove that every definition is a valid route. It’s
tempting to manage these text files with existing tools (e.g. version
control). But, bad merges can create disastrous outages where routes are lost or
incorrectly re-written by an API that can’t check anything more than whether the
file parses correctly.

Practically, porting web server config files to Envoy bootstrap config files is
a natural first step to try out Envoy. In order to put this into production,
it’s recommended to at least centralize these files behind a single service,
using a reference xDS implementation like
[go-control-plane](https://github.com/envoyproxy/go-control-plane). Allowing
multiple teams to edit these configs (#2 and #3, below) becomes a fragile part
of the system. Moving the source of truth behind an API allows concurrent
updates and prevents many nonsensical updates to routing definitions.

#2: Distribute control to teams
*******************************

Traffic management unlocks powerful workflows like **blue-green releases** and
**incremental migrations**. This makes it practical (and safe) for service teams
to **control the routes to the services they own**. Depending on your needs, you
either want to hide routes outside of their area of responsibility (to prevent
mis-clicks and accidents), or entirely prevent certain members from modifying
routes. Most of the time, this process should mirror the permissions for your
deployment process, as it’s a similar set of responsibilities.

#3: Design for change
*********************

Like infrastructure changes and code deploys, it’s vital to understand **when
routes changed** and **who changed them**. Many teams have found that when they
distribute responsibility for routing definition, the number of route changes
increases. For clarity, they keep a log of who made these changes. While
automating common route changes reduces overhead, it’s helpful to tag these
actions with the person likely associated with the change. For example,
automatic blue/green releases from master should be tagged with the person who
merged the last branch.

To be able to act on problems that come from routing changes, teams must know
how to **generate the changes** between two points in time and how to **roll
them back** if necessary. This isn’t only valuable when making further changes
(just as git history is useful when writing new code), but it should also be
exported to a centralized monitoring system. Having a diff of routing changes
means that problematic change-sets can be rolled back, giving operators more
tools to stabilize a system that’s misbehaving.
