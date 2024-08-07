.. _arch_overview_outlier_detection:

Outlier detection
=================

Outlier detection and ejection is the process of dynamically determining whether some number of
hosts in an upstream cluster are performing unlike the others and removing them from the healthy
:ref:`load balancing <arch_overview_load_balancing>` set. Performance might be along different axes
such as consecutive failures, temporal success rate, temporal latency, etc. Outlier detection is a
form of *passive* health checking. Envoy also supports :ref:`active health checking
<arch_overview_health_checking>`. *Passive* and *active* health checking can be enabled together or
independently, and form the basis for an overall upstream health checking solution.
Outlier detection is part of the :ref:`cluster configuration <envoy_v3_api_msg_config.cluster.v3.OutlierDetection>`
and it needs filters to report errors, timeouts, and resets. Currently, the following filters support
outlier detection: :ref:`http router <config_http_filters_router>`,
:ref:`tcp proxy <config_network_filters_tcp_proxy>`,
:ref:`redis proxy <config_network_filters_redis_proxy>` and :ref:`thrift proxy <config_network_filters_thrift_proxy>`.

Detected errors fall into two categories: externally and locally originated errors. Externally generated errors
are transaction specific and occur on the upstream server in response to the received request. For example, an HTTP server returning error code 500 or a redis server returning a payload which cannot be decoded. Those errors are generated on the upstream host after Envoy has connected to it successfully.
Locally originated errors are generated by Envoy in response to an event which interrupted or prevented communication with the upstream host. Examples of locally originated errors are timeout, TCP reset, inability to connect to a specified port, etc.

The type of detected errors depends on the filter type. The :ref:`http router <config_http_filters_router>` filter, for example,
detects locally originated errors (timeouts, resets - errors related to connection to upstream host) and because it
also understands the HTTP protocol it reports
errors returned by the HTTP server (externally generated errors). In such a scenario, even when the connection to the upstream HTTP server is successful,
the transaction with the server may fail.
By contrast, the :ref:`tcp proxy <config_network_filters_tcp_proxy>` filter does not understand any protocol above
the TCP layer and reports only locally originated errors.

Under the default configuration (:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *false*)
locally originated errors are not distinguished from externally generated (transaction) errors, all end up
in the same bucket, and are compared against the
:ref:`outlier_detection.consecutive_5xx<envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_5xx>`,
:ref:`outlier_detection.consecutive_gateway_failure<envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_gateway_failure>` and
:ref:`outlier_detection.success_rate_stdev_factor<envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_stdev_factor>`
configuration items. For example, if connection to an upstream HTTP server fails twice because of timeout and
then, after successful connection establishment, the server returns error code 500 then the total error count will be 3.

.. note::

  For TCP traffic, :ref:`outlier_detection.consecutive_5xx<envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_5xx>`
  is the correct parameter to set and transparently maps to TCP connection failures.

Outlier detection may also be configured to distinguish locally originated errors from externally originated (transaction) errors.
It is done via the
:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` configuration item.
In that mode locally originated errors are tracked by separate counters than externally originated
(transaction) errors and
the outlier detector may be configured to react to locally originated errors and ignore externally originated errors
or vice-versa.

It is important to understand that a cluster may be shared among several filter chains. If one filter chain
ejects a host based on its outlier detection type, other filter chains will be also affected even though their
outlier detection type would not have ejected that host.

.. _arch_overview_outlier_detection_algorithm:

Ejection algorithm
------------------

Depending on the type of outlier detection, ejection either runs inline (for example in the case of
consecutive 5xx) or at a specified interval (for example in the case of periodic success rate). The
ejection algorithm works as follows:

#. A host is determined to be an outlier.
#. It checks to make sure the number of ejected hosts is below the allowed threshold (specified via the
   :ref:`outlier_detection.max_ejection_percent<envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_percent>`
   setting). If the number of ejected hosts is above the threshold, the host is not ejected.
#. The host is ejected for some number of milliseconds. Ejection means that the host is marked
   unhealthy and will not be used during load balancing unless the load balancer is in a
   :ref:`panic <arch_overview_load_balancing_panic_threshold>` scenario. The number of milliseconds
   is equal to the :ref:`outlier_detection.base_ejection_time
   <envoy_v3_api_field_config.cluster.v3.OutlierDetection.base_ejection_time>` value
   multiplied by the number of times the host has been ejected in a row. This causes hosts to get ejected
   for longer and longer periods if they continue to fail. When ejection time reaches
   :ref:`outlier_detection.max_ejection_time<envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_time>` it does not increase any more.
   When the host becomes healthy, the ejection time
   multiplier decreases with time. The host's health is checked at intervals equal to
   :ref:`outlier_detection.interval<envoy_v3_api_field_config.cluster.v3.OutlierDetection.interval>`.
   If the host is healthy during that check, the ejection time multiplier is decremented. Assuming that the host stays healthy
   it would take approximately :ref:`outlier_detection.max_ejection_time<envoy_v3_api_field_config.cluster.v3.OutlierDetection.max_ejection_time>` /
   :ref:`outlier_detection.base_ejection_time<envoy_v3_api_field_config.cluster.v3.OutlierDetection.base_ejection_time>` *
   :ref:`outlier_detection.interval<envoy_v3_api_field_config.cluster.v3.OutlierDetection.interval>` seconds to bring down the ejection time to the minimum
   value :ref:`outlier_detection.base_ejection_time<envoy_v3_api_field_config.cluster.v3.OutlierDetection.base_ejection_time>`.
#. An ejected host will automatically be brought back into service after the ejection time has
   been satisfied. Generally, outlier detection is used alongside :ref:`active health checking
   <arch_overview_health_checking>` for a comprehensive health checking solution.

.. note::

  If :ref:`active health checking <arch_overview_health_checking>` is also configured, a successful active health check unejects the host and
  clears all outlier detection counters. If the host has not reached :ref:`unhealthy_threshold<envoy_v3_api_field_config.core.v3.HealthCheck.unhealthy_threshold>`
  failed health checks yet, a single successful health check will uneject the host. If the FAILED_ACTIVE_HC health flag is set for the host,
  :ref:`healthy_threshold<envoy_v3_api_field_config.core.v3.HealthCheck.healthy_threshold>` consecutive successful health checks
  will uneject the host (and clear the FAILED_ACTIVE_HC flag).
  If your active health check is not validating data plane traffic then in situations where
  active health checking passes but the traffic is failing, the endpoint will be unejected prematurely. To disable this option then set
  :ref:`outlier_detection.successful_active_health_check_uneject_host<envoy_v3_api_field_config.cluster.v3.OutlierDetection.successful_active_health_check_uneject_host>`
  configuration flag to ``false``.

Detection types
---------------

Envoy supports the following outlier detection types:

Consecutive 5xx
^^^^^^^^^^^^^^^

In the default mode (:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *false*) this detection type takes into account all generated errors: locally
originated and externally originated (transaction) errors.

.. note::

  Errors generated by non-HTTP filters, like :ref:`tcp proxy <config_network_filters_tcp_proxy>` or
  :ref:`redis proxy <config_network_filters_redis_proxy>` are internally mapped to HTTP 5xx codes and
  treated as such.

In split mode (:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *true*) this detection type takes into account only externally originated (transaction) errors, ignoring locally originated errors.
If an upstream host is an HTTP-server, only 5xx types of error are taken into account (see :ref:`Consecutive Gateway Failure<consecutive_gateway_failure>` for exceptions).
For redis servers, served via
:ref:`redis proxy <config_network_filters_redis_proxy>` only malformed responses from the server are taken into account.
Properly formatted responses, even when they carry an operational error (like index not found, access denied) are not taken into account.

If an upstream host returns some number of errors which are treated as consecutive 5xx type errors, it will be ejected.
The number of consecutive 5xx required for ejection is controlled by
the :ref:`outlier_detection.consecutive_5xx<envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_5xx>` value.

.. _consecutive_gateway_failure:

Consecutive Gateway Failure
^^^^^^^^^^^^^^^^^^^^^^^^^^^

In the default mode (:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *false*) this detection type takes into account a subset of 5xx errors, called "gateway errors" (502, 503 or 504 status code) and local origin failures, such as timeout, TCP reset etc.

In split mode (:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *true*) this detection type takes into account a subset of 5xx errors, called "gateway errors" (502, 503 or 504 status code) and is supported only by the :ref:`http router <config_http_filters_router>`.

If an upstream host returns some number of consecutive "gateway errors" (502, 503 or 504 status
code), it will be ejected.
The number of consecutive gateway failures required for ejection is controlled by
the :ref:`outlier_detection.consecutive_gateway_failure
<envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_gateway_failure>` value.

Consecutive Local Origin Failure
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This detection type is enabled only when :ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *true* and takes into account only locally originated errors (timeout, reset, etc).
If Envoy repeatedly cannot connect to an upstream host or communication with the upstream host is repeatedly interrupted, it will be ejected.
Various locally originated problems are detected: timeout, TCP reset, ICMP errors, etc. The number of consecutive
locally originated failures required for ejection is controlled
by the :ref:`outlier_detection.consecutive_local_origin_failure
<envoy_v3_api_field_config.cluster.v3.OutlierDetection.consecutive_local_origin_failure>` value.
This detection type is supported by :ref:`http router <config_http_filters_router>`,
:ref:`tcp proxy <config_network_filters_tcp_proxy>`  and :ref:`redis proxy <config_network_filters_redis_proxy>`.

Success Rate
^^^^^^^^^^^^

Success Rate based outlier detection aggregates success rate data from every host in a cluster. Then at given
intervals ejects hosts based on statistical outlier detection. Success Rate outlier detection will not be
calculated for a host if its request volume over the aggregation interval is less than the
:ref:`outlier_detection.success_rate_request_volume<envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_request_volume>`
value. Moreover, detection will not be performed for a cluster if the number of hosts
with the minimum required request volume in an interval is less than the
:ref:`outlier_detection.success_rate_minimum_hosts<envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_minimum_hosts>`
value.

In the default configuration mode (:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *false*)
this detection type takes into account all types of errors: locally and externally originated. The
:ref:`outlier_detection.enforcing_local_origin_success<envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_local_origin_success_rate>` config item is ignored.

In split mode (:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>` is *true*),
locally originated errors and externally originated (transaction) errors are counted and treated separately.
Most configuration items, namely
:ref:`outlier_detection.success_rate_minimum_hosts<envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_minimum_hosts>`,
:ref:`outlier_detection.success_rate_request_volume<envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_request_volume>`,
:ref:`outlier_detection.success_rate_stdev_factor<envoy_v3_api_field_config.cluster.v3.OutlierDetection.success_rate_stdev_factor>` apply to both
types of errors, but :ref:`outlier_detection.enforcing_success_rate<envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_success_rate>` applies
to externally originated errors only and :ref:`outlier_detection.enforcing_local_origin_success_rate<envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_local_origin_success_rate>`  applies to locally originated errors only.

.. _arch_overview_outlier_detection_failure_percentage:

Failure Percentage
^^^^^^^^^^^^^^^^^^

Failure Percentage based outlier detection functions similarly to success rate detection, in
that it relies on success rate data from each host in a cluster. However, rather than compare those
values to the mean success rate of the cluster as a whole, they are compared to a flat
user-configured threshold. This threshold is configured via the
:ref:`outlier_detection.failure_percentage_threshold<envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_threshold>`
field.

The other configuration fields for failure percentage based detection are similar to the fields for
success rate detection. Failure percentage based detection also obeys
:ref:`outlier_detection.split_external_local_origin_errors<envoy_v3_api_field_config.cluster.v3.OutlierDetection.split_external_local_origin_errors>`;
the enforcement percentages for externally- and locally-originated errors are controlled by
:ref:`outlier_detection.enforcing_failure_percentage<envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_failure_percentage>`
and
:ref:`outlier_detection.enforcing_failure_percentage_local_origin<envoy_v3_api_field_config.cluster.v3.OutlierDetection.enforcing_failure_percentage_local_origin>`,
respectively. As with success rate detection, detection will not be performed for a host if its
request volume over the aggregation interval is less than the
:ref:`outlier_detection.failure_percentage_request_volume<envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_request_volume>`
value. Detection also will not be performed for a cluster if the number of hosts with the minimum
required request volume in an interval is less than the
:ref:`outlier_detection.failure_percentage_minimum_hosts<envoy_v3_api_field_config.cluster.v3.OutlierDetection.failure_percentage_minimum_hosts>`
value.

.. _arch_overview_outlier_detection_grpc:

gRPC
----------------------

For gRPC requests, the outlier detection will use the HTTP status mapped from the `grpc-status <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md#responses>`_ response header.

.. _arch_overview_outlier_detection_logging:

Ejection event logging
----------------------

A log of outlier ejection events can optionally be produced by Envoy. This is extremely useful
during daily operations since global stats do not provide enough information on which hosts are
being ejected and for what reasons. The log is structured as protobuf-based dumps of
:ref:`OutlierDetectionEvent messages <envoy_v3_api_msg_data.cluster.v3.OutlierDetectionEvent>`.
Ejection event logging is configured in the Cluster manager :ref:`outlier detection configuration <envoy_v3_api_field_config.bootstrap.v3.ClusterManager.outlier_detection>`.

Configuration reference
-----------------------

* Cluster manager :ref:`global configuration <envoy_v3_api_field_config.bootstrap.v3.ClusterManager.outlier_detection>`
* Per cluster :ref:`configuration <envoy_v3_api_msg_config.cluster.v3.OutlierDetection>`
* Runtime :ref:`settings <config_cluster_manager_cluster_runtime_outlier_detection>`
* Statistics :ref:`reference <config_cluster_manager_cluster_stats_outlier_detection>`
