.. _common_configuration_rolling_upgrades:

Handling Rolling Upgrades and/or transient network failtures
============================================================

One of the biggest advantages of using Envoy in service mesh is that it frees up services 
from implementing complex resiliency features like circuit breaking, outlier detection and retries 
that enable services to be resilient to realities such as rolling upgrades, dynamic infrastructure, 
and network failures. Having these features implemented at Envoy not only improves the availability 
and resiliency of services but also brings in consistency in terms of the behaviour and observability. 

This section explains at a high level the configuration supported by Envoy and how these features can be 
used to together to handle these scenarios.

**Circuit Breaking** 

:ref:`Circuit Breaking<arch_overview_circuit_break>` is a critical component of distributed systems. 
Circuit breaking lets applications configure failure thresholds that ensure safe maximums, allows components
to fail quickly and apply back pressure as soon as possible. Applying right circuit breaking thresholds helps
to save resources which otherwise are wasted in waiting for requests (timeouts) or retrying requests unnecessarily. 
One of the main advantages of circuit breaking implementation in Envoy is that the circuit breaking limits are applied
at the network level.

**Retries**

Automatic :ref:`request retries<config_http_filters_router>` is another measure of ensuring service resilience. Request retries should typically be used to guard against 
transient failures. Envoy supports very rich set of configurable parameters that dictate what type of requests are retried, how many times 
the request should be retried, timeouts for retries etc.

*Retries in gRPC services*

For gRPC service, envoy looks at the grpc status in the response and attempts a retry based on the statuses configured in *x-retry-grpc-on*.

The following application status codes in gRPC are considered safe for automatic retry.

* Cancelled - Return this code if there is an error that can be retried in the service.
* Resource_Exhausted - Return this code if some of the resources that depends are exhausted in that instance so that retrying 
  to another instance would help. Please note that for shared resource exhaustion, returning this will not help. Instead :ref:`rate limiting<arch_overview_rate_limit>`
  should be used to handle such cases.

The  Http Status codes 502(Bad Gateway), 503(Service Unavailable) and 504(Gateway Timeout) are all mapped to gRPC status code *Unavailable*. 
This can also be considered safe for automatic retry.

An important consideration should be given to *idempotency* of a request while configuring retries.

Envoy also supports various extensions to the standard retry policy supported out of the box. 
The :ref:`retry plugins<arch_overview_http_retry_plugins>` allow you to customize the Envoy 
retry implementation to your application.

**Outlier Detection**

:ref:`Outlier detection<arch_overview_outlier_detection>` is a way of dynamically determining outlier 
hosts in the upstream cluster. If certain hosts behave differently from others like returning high 5xx 
or success rate differs  from other hosts etc. By detecting such hosts and ejecting them for a 
temporary period of time from the healthy load balancing set, Envoy can increase the success rate of a request. 
Envoy supports configuring outlier detection based on continuous 5xx, continuous gateway failures and success rate.

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

For the purpose of this specific use case, the retry budget for upstream cluster should be configured to enable and control concurrent retries. Please note 
that a low value of this. If the value configured is too low, some requests will not be retried and will result in :ref:`upstream_rq_retry_overflow <config_cluster_manager_cluster_stats>`
and if the value is configured is too high, the service can be overwhelmed with retry requests. 


*Outlier Detection*

.. code-block:: json

  {
     "consecutive_5xx": 5,
     "base_ejection_time": "30s",
     "max_ejection_percent": 50,
     "consecutive_gateway_failure": 5,
  }

This setting enables outlier detection if there are 5 consecutive 5xx or gateway failures and limit the number of hosts that are ejected to 50% of the upstream cluster size. 
This will allow us to keep the available hosts in the load balancer to be optimum for retries while ejecting truly bad hosts. Please note that once a host a ejected, it would be brought back 
for in to the pool after an ejection time is elapsed (which is equal to the base_ejection_time multiplied by the number of times the host has been ejected).

*Request Retry*

.. code-block:: json

  {
     "retry_on": "cancelled,connect-failure,gateway-error,refused-stream,resource-exhausted,unavailable",
     "num_retries": 1,
     "retry_host_predicate": [
     {
        "name": "envoy.retry_host_predicates.previous_hosts"
     }
    ],
    "host_selection_retry_max_attempts": "5"
  }

The request will be retried based on the conditions documented in retry_on. This setting also configures Envoy to use 
:ref:`Previous Host Retry Predicate<arch_overview_http_retry_plugins>` predicate that allows it to choose a different
host than the host where previous request has failed because typically failures on that same host are likely to continue 
for some time and immediate retry would have less chance of success. 
