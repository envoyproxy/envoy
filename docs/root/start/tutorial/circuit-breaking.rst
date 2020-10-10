
.. _circuit_breaking:

Circuit breaking
================

In the world of microservices, services often make calls to other services. But
what happens when a service is busy, or unable to respond to that call? How do
you avoid a failure in one part of your infrastructure cascading into other
parts of your infrastructure? One approach is to use circuit breaking.

Circuit breaking lets you configure failure thresholds that ensure safe
maximums after which these requests stop. This allows for a more graceful
failure, and time to respond to potential issues before they become larger.
It’s possible to implement circuit breaking in a few parts of your
infrastructure, but implementing these circuit breakers within services means
they are vulnerable to the same overload and failure we’re hoping to prevent.
Instead, we can configure circuit breaking at the network layer within Envoy,
and combine it with other traffic shaping patterns to ensure healthy and stable
infrastructure.

Configuring circuit breaking
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Envoy provides simple configuration for circuit breaking. Consider the
specifics of your system as you set up circuit breaking.

Circuit breaking is specified as part of a Cluster (a group of similar upstream
hosts) definition by adding a ``circuit_breakers`` field. Clusters are returned
from the Cluster Discovery Service (CDS), either in the bootstrap config, or a
remote CDS server .

Circuit Breaker Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here's what a simple circuit breaking configuration looks like:

.. code-block:: yaml

   circuit_breakers:
     thresholds:
     - priority: DEFAULT
       max_connections: 1000
       max_requests: 1000
     - priority: HIGH
       max_connections: 2000
       max_requests: 2000

In this example, there are a few fields that allow for a lot of service
flexibility:

 ``thresholds`` allows us to define priorities and limits for the type of traffic
 that our service responds to.

``priority`` refers to how routes defined as ``DEFAULT`` or ``HIGH`` are treated by
the circuit breaker. Using the settings above, we would want to set any
requests that shouldn’t wait in a long queue to HIGH. For example: POST
requests in a service where a user wants to make a purchase, or save their
state.

``max_connections`` are the maximum number of connections that Envoy will make to
our service clusters. The default for these is 1024, but in real-world
instances we may drastically lower them.

``max_requests`` are the maximum number of parallel requests that Envoy makes to
our service clusters. The default is also 1024.

Typical Circuit Breaker Policy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Essentially all clusters will benefit from a simple circuit breaker at the
network level. Because HTTP/1.1 and HTTP/2 have different connection behaviors
(one connection per request vs. many requests per connection), clusters with
different protocols will each use a different option:

 - For **HTTP/1.1** connections, use ``max_connections``.
 - For **HTTP/2** connections, use ``max_requests``.

Circuit breakers prevent catastrophic outages based on timeout failures, where a
single service cascades everywhere. The most insidious failures happen not
because a service is down, but because it’s hanging on to requests for tens of
seconds. Generally, it’s better for one service to be completely down than for
all services to be outside their SLO because they’re waiting for a timeout deep
in the stack.

A good value for either of these options depends on two things: the number of
connections/requests to your service and the typical request latency. For
example, an HTTP/1 service with 1,000 requests / second and average latency of 2
seconds will typically have 2,000 connections open at any given time. Because
circuit breakers trip when there are a large number of slower-than-normal
connections, consider a starting point for this service in the range of 10 x
2,000. This would open the breaker when most of the requests in the last 10
seconds have failed to return any response. Exactly what you pick will depend on
the spikiness of your load and how overprovisioned the service is.

Advanced circuit breaking
~~~~~~~~~~~~~~~~~~~~~~~~~

Now that you’ve seen a basic configuration and policy for circuit breaking,
we’ll discuss more advanced circuit breaking practices. These advanced
practices will add more resiliency to your infrastructure at the network level.

Break–on–latency
****************

As mentioned above, one of the most common use cases of circuit breakers is to
prevent failures that are caused when a service is excessively slow, but not
fully down. While Envoy doesn’t directly provide an option to trip the breaker
on latency, you can combine it with :ref:`automatic retries <automatic_retries>`
to emulate this behavior.

To break on an unexpected spike in slow requests, reduce the latency threshold
for retries and enable circuit break on lots of retries using the ``max_retries``
option.

If you do this, monitor the results closely! Many practitioners report that
setting too low a latency threshold is the only time they’ve *created* an
outage by adding circuit breakers. When this latency threshold too low, you can
DoS your services. Start higher than you think you need, and lower it over
time.

Configure breaking based on a long queue of retries
***************************************************

Even if you’re only :ref:`retrying requests <automatic_retries>`
on connection errors, it is valuable to set up circuit breaking. Because
retries have the  potential to increase the number of requests by 2x or more,
circuit breaking  using the ``max_retries`` parameter protects services from
being overloaded by  too many active retries. Set this value to a similar
number as ``max_connections`` or ``max_requests — a`` fraction of the total number of
requests the service typically handles in a 10-second window. If the service
has as many retries outstanding as the typical number of requests, it’s broken
and should be disabled.

Next Steps
**********

With circuit breaking configured, your service is equipped to help selectively
shed load when failure occurs, preventing it from cascading to multiple
services. Combining this tool with :ref:`automatic retries <automatic_retries>`
makes for robust services that are able to handle common issues at the network
level.
