.. _health_check:

Health check
============

The only thing better than finding and fixing problems quickly is not having
problems at all. While many issues are caused by bugs in code, applications
with thousands of hosts frequently run into problems with flaky hosts or
transient connectivity. These issues have little to do with the application
logic. To address these types of problems, Envoy has two strategies for
identifying failing infrastructure: Health Checking (active monitoring) and
Outlier Detection (passive monitoring).

Health Checking
~~~~~~~~~~~~~~~

A healthy host is one that can respond positively to a request. Envoy can be
configured to actively test hosts with
:ref:`Health checking <arch_overview_health_checking>`,
and it is defined on a per-:ref:`cluster <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>` (i.e. service)
basis. The simplest health check, or L3/L4 check, just ensures each
:ref:`endpoint <envoy_v3_api_file_envoy/config/endpoint/v3/endpoint.proto>` (i.e. port on a host or container)
is available, and it doesn’t depend on any application logic. Because the IP
and port are already specified as part of the endpoint, setting up a Health
Check doesn’t even require service-specific information.

A simple static YAML configuration may look like this:

.. code-block:: yaml

   health_checks:
   - timeout: 1s
     interval: 60s
     interval_jitter: 1s
     unhealthy_threshold: 3
     healthy_threshold: 3
     tcp_health_check: {}

Envoy also knows about HTTP, gRPC, and Redis protocols. For example, to test
for a non-5xx response, replace ``tcp_health_check`` with ``http_health_check``,
which defines the URL to test:

.. code-block:: yaml

   host: "servicehost"
   path: "/health"
   service_name: "authentication"

With this strategy, you can implement logic for services that have a custom
definition of “up”. While it’s easy to imagine writing comprehensive health
checks that look for all possible problems with a host, the reality is that the
check will never be full-fidelity. No matter how complex the active check is,
there will be scenarios where it returns “success” and the host is failing. To
figure out if a host is healthy enough to take traffic, it’s best to combine
health checking with outlier detection.

Outlier Detection
~~~~~~~~~~~~~~~~~

Unlike active health checking,
:ref:`Outlier Detection <arch_overview_outlier_detection>`
—sometimes called passive health checking—uses the **responses from real
requests to determine whether an endpoint is healthy**. Once an endpoint is
removed, Envoy uses a time-out based approach for re-insertion, where unhealthy
hosts are re-added to the cluster after a configurable interval. Each
subsequent removal increases the interval, so a persistently unhealthy endpoint
does as little damage as possible to user traffic.

Like health checks, outlier detection is configured per-cluster. This
configuration removes a host for 30 seconds when it returns 3 consecutive 5xx
errors:

.. code-block:: yaml

   consecutive_5xx: "3"
   base_ejection_time: "30s"

When enabling outlier detection on a new service, it can help to start with a
less stringent set of rules. A good place to start is to only eject hosts with
gateway connectivity errors (HTTP 503), and only eject them 10% of the time. By
ramping up the enforcement rate, you can avoid kicking a bunch of hosts out of
a service for being flaky, which would potentially create more problems than it
would solve:

.. code-block:: yaml

   consecutive_gateway_failure: "3"
   base_ejection_time: "30s"
   enforcing_consecutive_gateway_failure: "10"

On the other end of the spectrum, high-traffic, stable services can use
statistics to eject hosts that are abnormally error-prone. This configuration
would eject any endpoint whose error rate is more than 1 standard deviation
below the average for the cluster. Note that this requires sustained traffic:
the statistics are evaluated every 10 seconds, and the algorithm isn’t run for
any host with fewer than 500 requests / 10 seconds.

.. code-block:: yaml

   interval: "10s"
   base_ejection_time: "30s"
   success_rate_minimum_hosts: "10"
   success_rate_request_volume: "500"
   success_rate_stdev_factor: "1000" # divided by 1000 to get a double

In all cases, the endpoints removed do not exceed the ``max_ejection_percent`` of
the cluster, and they are  re-inserted after their timeout
(``base_ejection_time`` * number of ejections) expires.

Implementing Health Checking
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Health checking is one of the easiest ways to take advantage of Envoy’s ability
to make your services more reliable—and you don't even have to write any new
code. Remember that health checks are only for **host health**, and not
**service health**. The goal is to auto-heal your service, not detect and fix
bad code.

To build a practical picture of host health, it’s best to combine both active
and passive health checking. At scale, passive checks are more robust, so lean
on them as the primary check. A good general strategy is to reject any endpoint
with five 5xx responses in a row (outlier detection), or one that Envoy can’t
proactively connect to (health checking). As mentioned above, if your service
has enough traffic for it, statistical outlier detection is far more robust
than simple consecutive errors.

The above approach should work for most services, and it doesn’t require any
new code. **Resist the urge to add complex active health checking, as these
checks will never be perfect.** That said, there are three scenarios where
adding a more complex health check will likely improve your results:

If your service does **meaningful initialization work** that it performs
**after it starts taking traffic**, an active health check can prevent a spike
in errors when new hosts come online. In most cases, services with this
behavior were simply never written to be immediately available and correct.
Ideally, you’d fix the service to finish its initialization before serving
traffic, but if that’s not possible, adding a health check can mitigate these
transient errors.

More broadly, if your system has a **custom definition of health** that isn’t
easily derived from response data, an active health check can help. For
instance, Cassandra auto-balances its data across all available hosts, so you
could mark hosts under particularly heavy replication load unavailable for read
requests.

Finally, if the **amount of traffic is low but important**, an active health
check can give you early warning that a host is misbehaving before a user
connects. A service that updates payment information may only have a handful of
requests a day, but because it has the possibility to charge a customer’s
credit card, it’s difficult to use other resilience strategies like retries.
Health checking makes sure all endpoints can get the necessary DB connections
at all times, marking unreliable nodes as down before they make third-party
calls.

Logging ejections from the cluster will help you tune any rules you put in
place and prevent hosts from flapping in and out of the cluster.

For more details, see:

- :ref:`Health checking overview <arch_overview_health_checking>`, in the Envoy docs
- :ref:`Outlier detection overview <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>` in the Envoy docs
- :ref:`Cluster configuration <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>` in the Envoy docs
