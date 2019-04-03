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

Ejection algorithm
------------------

Depending on the type of outlier detection, ejection either runs inline (for example in the case of
consecutive 5xx) or at a specified interval (for example in the case of periodic success rate). The
ejection algorithm works as follows:

#. A host is determined to be an outlier.
#. If no hosts have been ejected, Envoy will eject the host immediately. Otherwise, it checks to make
   sure the number of ejected hosts is below the allowed threshold (specified via the
   :ref:`outlier_detection.max_ejection_percent<envoy_api_field_cluster.OutlierDetection.max_ejection_percent>`
   setting). If the number of ejected hosts is above the threshold, the host is not ejected.
#. The host is ejected for some number of milliseconds. Ejection means that the host is marked
   unhealthy and will not be used during load balancing unless the load balancer is in a
   :ref:`panic <arch_overview_load_balancing_panic_threshold>` scenario. The number of milliseconds
   is equal to the :ref:`outlier_detection.base_ejection_time_ms
   <envoy_api_field_cluster.OutlierDetection.base_ejection_time>` value
   multiplied by the number of times the host has been ejected. This causes hosts to get ejected
   for longer and longer periods if they continue to fail.
#. An ejected host will automatically be brought back into service after the ejection time has
   been satisfied. Generally, outlier detection is used alongside :ref:`active health checking
   <arch_overview_health_checking>` for a comprehensive health checking solution.

Detection types
---------------

Envoy supports the following outlier detection types:

Consecutive 5xx
^^^^^^^^^^^^^^^

If an upstream host returns some number of consecutive 5xx, it will be ejected. Note that in this
case a 5xx means an actual 5xx respond code, or an event that would cause the HTTP router to return
one on the upstream's behalf (reset, connection failure, etc.). The number of consecutive 5xx
required for ejection is controlled by the :ref:`outlier_detection.consecutive_5xx
<envoy_api_field_cluster.OutlierDetection.consecutive_5xx>` value.

Consecutive Gateway Failure
^^^^^^^^^^^^^^^^^^^^^^^^^^^

If an upstream host returns some number of consecutive "gateway errors" (502, 503 or 504 status
code), it will be ejected. Note that this includes events that would cause the HTTP router to
return one of these status codes on the upstream's behalf (reset, connection failure, etc.). The
number of consecutive gateway failures required for ejection is controlled by
the :ref:`outlier_detection.consecutive_gateway_failure
<envoy_api_field_cluster.OutlierDetection.consecutive_gateway_failure>` value.

Success Rate
^^^^^^^^^^^^

Success Rate based outlier ejection aggregates success rate data from every host in a cluster. Then at given
intervals ejects hosts based on statistical outlier detection. Success Rate outlier ejection will not be
calculated for a host if its request volume over the aggregation interval is less than the
:ref:`outlier_detection.success_rate_request_volume<envoy_api_field_cluster.OutlierDetection.success_rate_request_volume>`
value. Moreover, detection will not be performed for a cluster if the number of hosts
with the minimum required request volume in an interval is less than the
:ref:`outlier_detection.success_rate_minimum_hosts<envoy_api_field_cluster.OutlierDetection.success_rate_minimum_hosts>`
value.

.. _arch_overview_outlier_detection_logging:

Ejection event logging
----------------------

A log of outlier ejection events can optionally be produced by Envoy. This is extremely useful
during daily operations since global stats do not provide enough information on which hosts are
being ejected and for what reasons. The log is structured as protobuf-based dumps of
:ref:`OutlierDetectionEvent messages <envoy_api_msg_data.cluster.v2alpha.OutlierDetectionEvent>`.
Ejection event logging is configured in the Cluster manager :ref:`outlier detection configuration <envoy_api_field_config.bootstrap.v2.ClusterManager.outlier_detection>`.

Configuration reference
-----------------------

* Cluster manager :ref:`global configuration <envoy_api_field_config.bootstrap.v2.ClusterManager.outlier_detection>`
* Per cluster :ref:`configuration <envoy_api_msg_cluster.OutlierDetection>`
* Runtime :ref:`settings <config_cluster_manager_cluster_runtime_outlier_detection>`
* Statistics :ref:`reference <config_cluster_manager_cluster_stats_outlier_detection>`
