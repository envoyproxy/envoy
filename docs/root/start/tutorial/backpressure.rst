.. _backpressure:

Backpressure
============

A Guide to Envoy’s Backpressure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

One of the biggest advantages of a service mesh is that it frees up clients from
implementing features like rate limiting, retries, or timeouts. It’s easy to
think that with a service proxy like Envoy handling these features, there’s no
further work to be done. While this is almost true, if there’s a real failure,
(e.g. multiple retries that never succeed), the client will ultimately have to
handle it.

Whether you handle this with custom code in each service or with a set of thin
client libraries is up to you, but here are the behaviors you can expect to see
from Envoy when things go wrong with HTTP/1, HTTP/2, or gRPC requests.

Two quick definitions, used by Envoy:

- Upstream connections are the service Envoy is initiating the connection to.
- Downstream connections are the client that is initiating a request through
  Envoy.

These terms take their origin from traditional computer networking; information
flows like water through Envoy from `upstream <https://en.wikipedia.org/wiki/Upstream_(networking)>`_
(the service that eventually responds to the request) to `downstream <https://en.wikipedia.org/wiki/Downstream_(networking)>`_
(the caller making the request). In a service mesh, all services are often co-located in the
same cloud, but there's still utility in thinking in terms of traffic flow.

Envoy Failure Modes
~~~~~~~~~~~~~~~~~~~

The following is a comprehensive list of ways Envoy can send an unsuccessful
response to the downstream client.

Rate Limiting Failure
*********************

**What it is:** The :ref:`rate limiting <arch_overview_global_rate_limit>`
service determined that this request exceeds the quotas it sets and should be
tried again later.

**Result:** The client receives an `HTTP 429 <https://httpstatuses.com/429>`_
response.

Timeout Failure
***************

**What it is:** The upstream connection took longer to respond than the timeout
set on the route.

**Result:** The client receives an `HTTP 504 <https://httpstatuses.com/504>`_
(Read More)

Empty Cluster
*************

**What it is:** There are no hosts available in a cluster. Note that this is NOT
what happens when all hosts fail either health checks or outlier detection. In
either of those situations, Envoy will notice the number of hosts is below the
panic threshold and balance across all known hosts.

**Result:** Returns an `HTTP 503 <https://httpstatuses.com/503>`_ with the body
text “no healthy upstream”.

Unresponsive Upstream
*********************

**What it is:** The upstream host is unresponsive.

**Result:** Returns an `HTTP 503 <https://httpstatuses.com/503>`_ with the body
text “upstream connect error or disconnect/reset before headers”.

### Circuit Breaker Failed Open

**What it is:** Due to too many unsuccessful or outstanding requests to this
upstream, Envoy has tripped the circuit breaker and is failing the request.

**Result:** The client receives an `HTTP 503 <https://httpstatuses.com/503>`_ with
the ``x-envoy-overloaded: true`` header set. (Read More)

Maintenance mode
****************

**What it is:** Routes to this cluster have been configured with
``upstream.maintenance_mode.<cluster_name>``, and this request is in the fraction
of requests that is unsuccessful

**Result:** Returns an `HTTP 503 <https://httpstatuses.com/503>`_ with the
``x-envoy-overloaded: true`` header set. (Read More)

Domain or Route not found
*************************

**What it is:** Envoy is not configured to serve this URL.

**Result:** The client receives an `HTTP 404 <https://httpstatuses.com/404>`_.

Note: Envoy only supports HTTP/1.1 and newer, which requires the presence of a
``Host`` header. In particular, this means that requests to ``127.0.0.1`` or the IP
that Envoy is listening on will almost always return a 404, even if Envoy is
correctly configured to serve one or more domains.

Upstream Connection Closed
**************************

**What it is:** When the upstream closes the connection before the response is
finished sending, Envoy cannot send a complete response to the downstream.

**Result:** This depends on whether the downstream has started receiving data.

 - If it has not (i.e. the upstream disconnects quickly), the downstream
   connection is reset.
 - If it has, the downstream receives an `HTTP 503 <https://httpstatuses.com/503>`_
   and the body text “upstream connect error or disconnect/reset before headers”

Envoy is Overloaded
*******************

**What it is:** When Envoy itself is overloaded, it will stall. Envoy stops
reading from the downstream client when it wants to exert back pressure (e.g.,
local kernel buffer fills, then remote kernel buffer, then client blocks).

**Result:** Downstream connections are blocked with no error.

What Clients Should Handle
~~~~~~~~~~~~~~~~~~~~~~~~~~

In addition to the errors above, clients should also handle scenarios that
out-of-process proxies cannot deal with. These include:

- Thread-local circuit breakers
- Timeouts if Envoy is blocking
- A code path that returns a sensible result when a failure has occurred

Next Steps
~~~~~~~~~~

Once your client libraries can handle it, go implement some of Envoy's
resilience features, such as:

- :ref:`Automatic retries <automatic_retries>`
- :ref:`Circuit breaking <circuit_breaking>`
- :ref:`Health checks <health_check>`
