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
Outlier detection is part of :ref:`cluster configuration <envoy_api_msg_cluster.OutlierDetection>`, 
but it needs network filters to report errors, timeouts, resets. Currently the following filters support
outlier detection: :ref: `http router <config_http_filters_router>`, 
:ref: `tcp proxy <config_network_filters_tcp_proxy>`  and :ref: `redis proxy <config_network_filters_redis_proxy>`.
There are several types of outlier detection and not each filter supports all types. For example, 
:ref: `http router <config_http_filters_router>` checks HTTP error codes and will eject a host returning 
5xx HTTP error codes, but :ref: `tcp proxy <config_network_filters_tcp_proxy>` only checks for connection errors
and will not eject such host because it does not check payload above TCP level. 
It is important to understand that a cluster may be shared among several filter chains. If one filter chain
ejects a host based on its outlier detection type, other filter chains will be also affected even though their
outlier detection type would not eject that host.

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

If an upstream host returns some number of consecutive 5xx, it will be ejected. 
The number of consecutive 5xx required for ejection is controlled by 
the :ref:`outlier_detection.consecutive_5xx
<envoy_api_field_cluster.OutlierDetection.consecutive_5xx>` value. Certain error status codes 
(502, 503 and 504) are considered Gateway Failures not as 5xx error codes. This detection type 
is supported only by :ref: `http router <config_http_filters_router>`.

Consecutive Gateway Failure
^^^^^^^^^^^^^^^^^^^^^^^^^^^

If an upstream host returns some number of consecutive "gateway errors" (502, 503 or 504 status
code), it will be ejected. 
The number of consecutive gateway failures required for ejection is controlled by
the :ref:`outlier_detection.consecutive_gateway_failure
<envoy_api_field_cluster.OutlierDetection.consecutive_gateway_failure>` value.
This detection type is supported only by :ref: `http router <config_http_filters_router>`.

Consecutive Connect Failure
^^^^^^^^^^^^^^^^^^^^^^^^^^^

If Envoy repeatedly cannot connect to an upstream host because of a network problem, it will be ejected.
Various network problems are detected: timeout, TCP reset, ICMP errors, etc. The number of consecutive
connect failures required for ejection is controlled 
by the :ref: `outlier_detection.consecutive_connect_failure 
<envoy_api_field_cluster.OutlierDetection.consecutive_connect_failure>` value.
This detection type is supported by :ref: `http router <config_http_filters_router>`, 
:ref: `tcp proxy <config_network_filters_tcp_proxy>`  and :ref: `redis proxy <config_network_filters_redis_proxy>`.

Success Rate
^^^^^^^^^^^^

Success Rate based outlier ejection aggregates success rate data from every host in a cluster. Then at given
intervals ejects hosts based on statistical outlier detection. Success Rate outlier ejection will not be
calculated for a host if its request volume over the aggregation interval is less than the
:ref:`outlier_detection.success_rate_request_volume<envoy_api_field_cluster.OutlierDetection.success_rate_request_volume>`
value. Moreover, detection will not be performed for a cluster if the number of hosts
with the minimum required request volume in an interval is less than the
:ref:`outlier_detection.success_rate_minimum_hosts<envoy_api_field_cluster.OutlierDetection.success_rate_minimum_hosts>`
value. This detection type is supported by :ref: `http router <config_http_filters_router>`.

Consecutive Server Request Failure 
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If Envoy is able to connect to an upstream host, but server transaction repeatedly fails with that  
host, it will be ejected. The number of consecutive server transaction failures required for ejection 
is controlled by :ref: `outlier_detection.consecutive_server_request_failure
<envoy_api_field_cluster.OutlierDetection.consecutive_server_request_failure>`. 
This detection type is supported by :ref: `redis proxy <config_network_filters_redis_proxy>`.


Ejection event logging
----------------------

A log of outlier ejection events can optionally be produced by Envoy. This is extremely useful
during daily operations since global stats do not provide enough information on which hosts are
being ejected and for what reasons. The log uses a JSON format with one object per line:

.. code-block:: json

  {
    "time": "...",
    "secs_since_last_action": "...",
    "cluster": "...",
    "upstream_url": "...",
    "action": "...",
    "type": "...",
    "num_ejections": "...",
    "enforced": "...",
    "host_success_rate": "...",
    "cluster_success_rate_average": "...",
    "cluster_success_rate_ejection_threshold": "..."
  }

time
  The time that the event took place.

secs_since_last_action
  The time in seconds since the last action (either an ejection or unejection)
  took place. This value will be ``-1`` for the first ejection given there is no
  action before the first ejection.

cluster
  The :ref:`cluster <envoy_api_msg_Cluster>` that owns the ejected host.

upstream_url
  The URL of the ejected host. E.g., ``tcp://1.2.3.4:80``.

action
  The action that took place. Either ``eject`` if a host was ejected or ``uneject`` if it was
  brought back into service.

type
  If ``action`` is ``eject``, specifies the type of ejection that took place. Currently type can
  be one of ``5xx``, ``GatewayFailure``, ``SuccessRate``, ``ConnectFailure`` or ``MalformedPayload``.

num_ejections
  If ``action`` is ``eject``, specifies the number of times the host has been ejected
  (local to that Envoy and gets reset if the host gets removed from the upstream cluster for any
  reason and then re-added).

enforced
  If ``action`` is ``eject``, specifies if the ejection was enforced. ``true`` means the host was ejected.
  ``false`` means the event was logged but the host was not actually ejected.

host_success_rate
  If ``action`` is ``eject``, and ``type`` is ``SuccessRate``, specifies the host's success rate
  at the time of the ejection event on a ``0-100`` range.

.. _arch_overview_outlier_detection_ejection_event_logging_cluster_success_rate_average:

cluster_success_rate_average
  If ``action`` is ``eject``, and ``type`` is ``SuccessRate``, specifies the average success
  rate of the hosts in the cluster at the time of the ejection event on a ``0-100`` range.

.. _arch_overview_outlier_detection_ejection_event_logging_cluster_success_rate_ejection_threshold:

cluster_success_rate_ejection_threshold
  If ``action`` is ``eject``, and ``type`` is ``SuccessRate``, specifies success rate ejection
  threshold at the time of the ejection event.

Configuration reference
-----------------------

* Cluster manager :ref:`global configuration <envoy_api_field_config.bootstrap.v2.ClusterManager.outlier_detection>`
* Per cluster :ref:`configuration <envoy_api_msg_cluster.OutlierDetection>`
* Runtime :ref:`settings <config_cluster_manager_cluster_runtime_outlier_detection>`
* Statistics :ref:`reference <config_cluster_manager_cluster_stats_outlier_detection>`
