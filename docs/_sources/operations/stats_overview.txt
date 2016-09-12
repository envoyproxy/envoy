.. _operations_stats:

Statistics overview
===================

Envoy outputs numerous statistics which depend on how the server is configured. They can be seen
locally via the :http:get:`/stats` command and are typically sent to a :ref:`statsd cluster
<arch_overview_statistics>`. The statistics that are output are documented in the relevant
sections of the :ref:`configuration guide <config>`. Some of the more important statistics that will
almost always be used can be found in the following sections:

* :ref:`HTTP connection manager <config_http_conn_man_stats>`
* :ref:`Upstream cluster <config_cluster_manager_cluster_stats>`
