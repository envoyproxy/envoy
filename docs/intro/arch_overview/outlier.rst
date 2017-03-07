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
#. Envoy checks to make sure the number of ejected hosts is below the allowed threshold (specified
   via the :ref:`outlier_detection.max_ejection_percent 
   <config_cluster_manager_cluster_outlier_detection>` value.
   If the number of ejected hosts is above the threshold the host is not ejected.
#. The host is ejected for some number of milliseconds. Ejection means that the host is marked
   unhealthy and will not be used during load balancing unless the load balancer is in a
   :ref:`panic <arch_overview_load_balancing_panic_threshold>` scenario. The number of milliseconds
   is equal to the :ref:`outlier_detection.base_ejection_time_ms
   <config_cluster_manager_cluster_outlier_detection>` value
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
<config_cluster_manager_cluster_outlier_detection>` value.

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
    "num_ejections": "..."
  }

time
  The time that the event took place.

secs_since_last_action
  The time in seconds since the last action (either an ejection or unejection)
  took place. This time will be -1 for the first ejection given there is no
  action before the first ejection.

cluster
  The :ref:`cluster <config_cluster_manager_cluster>` that owns the ejected host.

upstream_url
  The URL of the ejected host. E.g., ``tcp://1.2.3.4:80``.

action
  The action that took place. Either ``eject`` if a host was ejected or ``uneject`` if it was
  brought back into service.

type
  If ``action`` is ``eject``, species the type of ejection that took place. Currently this can
  only be ``5xx``.

num_ejections
  The number of times the host has been ejected (local to that Envoy and gets reset if the host
  gets removed from the upstream cluster for any reason and then re-added).

Configuration reference
-----------------------

* Cluster manager :ref:`global configuration <config_cluster_manager_outlier_detection>`
* Per cluster :ref:`configuration <config_cluster_manager_cluster_outlier_detection>`
* Runtime :ref:`settings <config_cluster_manager_cluster_runtime_outlier_detection>`
* Statistics :ref:`reference <config_cluster_manager_cluster_stats_outlier_detection>`
