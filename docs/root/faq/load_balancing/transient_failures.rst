.. _common_configuration_transient_failures:

How do I handle transient failures?
===================================

One of the biggest advantages of using Envoy in a service mesh is that it frees up services
from implementing complex resiliency features like circuit breaking, outlier detection and retries
that enable services to be resilient to realities such as rolling upgrades, dynamic infrastructure,
and network failures. Having these features implemented at Envoy not only improves the availability
and resiliency of services but also brings in consistency in terms of the behaviour and observability.

This section explains at a high level the configuration supported by Envoy and how these features can be
used together to handle these scenarios.

Circuit Breaking
----------------

:ref:`Circuit Breaking <arch_overview_circuit_break>` is a critical component of distributed systems.
Circuit breaking lets applications configure failure thresholds that ensure safe maximums, allowing components
to fail quickly and apply back pressure as soon as possible. Applying correct circuit breaking thresholds helps
to save resources which otherwise are wasted in waiting for requests (timeouts) or retrying requests unnecessarily.
One of the main advantages of the circuit breaking implementation in Envoy is that the circuit breaking limits are applied
at the network level.

.. _common_configuration_transient_failures_retries:

Retries
-------

Automatic :ref:`request retries <config_http_filters_router>` is another method of ensuring service resilience. Request retries should
typically be used to guard against transient failures. Envoy supports very rich set of configurable parameters that dictate what type
of requests are retried, how many times the request should be retried, timeouts for retries, etc.

Retries in gRPC services
------------------------

For gRPC services, Envoy looks at the gRPC status in the response and attempts a retry based on the statuses configured in *x-retry-grpc-on*.

The following application status codes in gRPC are considered safe for automatic retry.

* *CANCELLED* - Return this code if there is an error that can be retried in the service.
* *RESOURCE_EXHAUSTED* - Return this code if some of the resources that service depends on are exhausted in that instance so that retrying
  to another instance would help. Please note that for shared resource exhaustion, returning this will not help. Instead :ref:`rate limiting <arch_overview_global_rate_limit>`
  should be used to handle such cases.

The HTTP Status codes *502 (Bad Gateway)*, *503 (Service Unavailable)* and *504 (Gateway Timeout)* are all mapped to gRPC status code *UNAVAILABLE*.
This can also be considered safe for automatic retry.

The idempotency of a request is an important consideration when configuring retries.

Envoy also supports extensions to its retry policies. The :ref:`retry plugins <arch_overview_http_retry_plugins>`
allow you to customize the Envoy retry implementation to your application.

Outlier Detection
-----------------

:ref:`Outlier detection <arch_overview_outlier_detection>` is a way of dynamically detecting misbehaving hosts
in the upstream cluster. By detecting such hosts and ejecting them for a temporary period of time from the healthy
load balancing set, Envoy can increase the success rate of a cluster. Envoy supports configuring outlier detection
based on continuous *5xx*, continuous gateway failures and success rate.

Envoy also allows you to configure the ejection period.

**Configuration**

The following settings help to optimize some combination of:

* Maximum request success for common scenarios (i.e. rolling upgrade)
* Speed
* Avoid cascading failures


*Circuit Breaker*

.. code-block:: json

  {
     "thresholds": [
       {
         "max_retries": 10,
       }
    ]
  }

For the purpose of this specific use case, the retry budget for upstream cluster should be configured to
enable and control concurrent retries. If the value configured is too low, some requests will not be retried,
which can be measured via :ref:`upstream_rq_retry_overflow <config_cluster_manager_cluster_stats>`.
If the value configured is too high, the service can be overwhelmed with retry requests.


*Outlier Detection*

.. code-block:: json

  {
     "consecutive_5xx": 5,
     "base_ejection_time": "30s",
     "max_ejection_percent": 50,
     "consecutive_gateway_failure": 5,
  }

This setting enables outlier detection if there are 5 consecutive *5xx* or *gateway failures*
and limits the number of hosts that are ejected to 50% of the upstream cluster size. This configuration
places a safe limit on the number of hosts removed. Please note that once a host a ejected, it will be returned
to the pool after an ejection time is elapsed (which is equal to the *base_ejection_time* multiplied by the number
of times the host has been ejected).

*Request Retry*

.. code-block:: json

  {
     "retry_on": "cancelled,connect-failure,gateway-error,refused-stream,reset,resource-exhausted,unavailable",
     "num_retries": 1,
     "retry_host_predicate": [
     {
        "name": "envoy.retry_host_predicates.previous_hosts"
     }
    ],
    "host_selection_retry_max_attempts": "5"
  }

The request will be retried based on the conditions documented in *retry_on*. This setting also configures Envoy to use
:ref:`Previous Host Retry Predicate <arch_overview_http_retry_plugins>` that allows it to choose a different
host than the host where previous request has failed, because typically failures on that same host are likely to continue
for some time and immediate retry would have less chance of success.
