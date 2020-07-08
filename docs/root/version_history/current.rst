1.16.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* stats: added optional histograms to :ref:`cluster stats <config_cluster_manager_cluster_stats_request_response_sizes>` 
  that track headers and body sizes of requests and responses.

Deprecated
----------
* The :ref:`track_timeout_budgets <envoy_api_field_config.cluster.v3.Cluster.track_timeout_budgets>` 
  field has been deprecated in favor of `timeout_budgets` part of an :ref:`Optional Configuration <envoy_api_field_config.cluster.v3.Cluster.track_cluster_stats>`.
