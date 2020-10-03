.. _automatic_retries:

Automatic retries
=================

Automatic retries are a powerful way to add resilience to your system with
essentially no changes to your services. In many systems, failed requests can
be retried without any negative consequences, shielding users from transient
issues.

Envoy provides a simple configuration option for retrying requests. Consider
specifics of your system as you set up retries across each route:

- Choose appropriate defaults
- Limit retry-able requests
- Consider the calling context

Choose Appropriate Defaults
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Retries are specified as part of a route definition by adding a retry_policy
field to the route_action. In the API, this would be returned from the Route
Discovery Service (RDS).

A typical Envoy retry policy
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here's what a simple retry configuration looks like for a single route:

.. code-block:: yaml

   retry_on: "5xx"
   num_retries: 3
   per_try_timeout_ms: 2000

The `retry_on` parameter specifies **which types of responses to retry** this
request on. 5xx is a good place to start, as it will retry all server errors.
There are more specific subsets that Envoy supports (e.g. gateway-error,
connect-failure, and refused-stream), but all of these are caught with 5xx.

By default, Envoy will set the **number of retries** to one with
`num_retries`. There’s little downside to increasing this to three, especially
for relatively short requests, as Envoy will limit the total time spent to the
overall request timeout, including the initial request and all retries.

The `per_try_timeout_ms` field sets a **timeout for each retry** in
milliseconds. Without this parameter, any request that times out will not be
retried, since the default is the same as the calling request’s timeout. While
it’s not a big deal to leave this out, setting it to the 99th percentile of
normal latency allows Envoy to retry requests that are taking a long time due to
a failure. (Note that this limit may be longer than the total request
timeout — more on that below.)

Limit Retry-able Requests
~~~~~~~~~~~~~~~~~~~~~~~~~

Once you have your defaults in place, there are several types of calls for
which it does not make sense to retry requests.

First, **do not retry requests where retrying would change the result**, such
as non-idempotent transactions. Most frameworks for monolithic services wrap
all requests in a DB transaction, guaranteeing any failure will roll back all
state changes, allowing the caller to try again. Unfortunately, in the
microservices world, an intermediate service may not be as diligent in
unwinding partial work across several services on a failure. Even worse, the
unwinding may fail, or the caller won’t be informed of the final state. In
general, enabling retries for all read requests is safe and effective, since
most systems tend to be read-heavy. Since routes in Envoy can be specified by
HTTP method, retrying GET requests is a good place to start.

Similarly, **do not retry expensive requests**, especially if they cannot be
canceled. This is subjective, because these requests can “correctly” be
retried. The problem is that retrying them may run the system out of resources,
creating a set of cascading failures. As a general rule, don’t retry requests
where the user would be impatiently waiting for the result. User logins should
almost always be retried quickly; complex analytics calculations should fail
back to the user and let either the UI or the user retry immediately.

For complex cases, remember that any of this configuration can be overridden
with HTTP headers. It’s better to configure routes to be conservative with
their retries and allow specific calling code to request more aggressive retry
behavior.

Consider the Calling Context
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For internal service calls, it’s important to consider the restrictions imposed
on the caller as well.

Since Envoy will limit the total duration of retries, consider the relationship
between the route's global timeout
([`timeout_ms`](https://www.envoyproxy.io/docs/envoy/latest/api-v1/route_config/route.html#config-http-conn-man-route-table-route-timeout)),
the upstream routes' timeout (also
([`timeout_ms`](https://www.envoyproxy.io/docs/envoy/latest/api-v1/route_config/route.html#config-http-conn-man-route-table-route-timeout)),
the per-retry timeout
([`per_try_timeout_ms`](https://www.envoyproxy.io/docs/envoy/latest/api-v1/route_config/route.html#config-http-conn-man-route-table-route-retry)),
and the number of retries
([`num_retries`](https://www.envoyproxy.io/docs/envoy/latest/api-v1/route_config/route.html#config-http-conn-man-route-table-route-retry)).
In general, it's better to fail quickly and retry than to let long requests
attempt to finish. This will, perhaps counterintuitively, increase success rates
and decrease most latency.

As an example, consider a request with a 500ms timeout that makes a single
upstream call with a maximum of 3 retries, limited to 250ms each. The problem
here is the # of retries times the individual retry limit is significantly
higher than the global timeout, so Envoy can’t make more than 2 calls before the
global timeout fails the request. This isn’t fundamentally bad, but the 250ms
timeout doesn't allow a full retry on timeout. (Sometimes failures are quick,
so Envoy will be able to complete the second request, and having a lot of
retries will help.) As a starting point, lowering the upstream timeout to 100ms
will allow several retries, accounting for the jitter that Envoy adds between
calls.

On the other hand, if the request makes many parallel requests (“high fan-out”)
without an aggressive global timeout, adding retries will result in consistently
poor performance. Imagine a service (with no global timeout) that makes 100
requests with an average of 150ms latency and a 500ms timeout on each upstream
called. Without retries, the request will be bounded at ~500ms -- either the
request will fail when some of the upstream calls fail, or it will
succeed. Adding 3 retries will cause this service to shoot from 500ms to
2,000ms on failure — a huge slowdown, which is only compounded in a service mesh
with deep calls stacks. This kind of added latency may cause more failures than
they fix! To avoid this, make sure to add a caller timeout to any service that
has high-fanout before adding retries to its upstream calls.

Next Steps
~~~~~~~~~~

Finally, it is strongly recommended that you set up Global Circuit Breaking in
conjunction with automatic retries. Retrying error requests 3x can triple the
volume of error traffic, making Envoy an amplifier for a misconfigured calling
service. Global circuit breaking helps selectively shed load when this sort of
failure occurs, preventing it from cascading to multiple services.

For more detail, and advanced configuration information, read about them in the
[Envoy docs](https://www.envoyproxy.io/docs/envoy/v1.8.0/api-v1/route_config/route.html#config-http-conn-man-route-table-route-retry).
