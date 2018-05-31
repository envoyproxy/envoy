.. _config_http_filters_rbac:

Role Based Access Control (RBAC) Filter
=======================================

The RBAC filter is used to authorize actions (permissions) by identified downstream clients
(principals). This is useful to explicitly manage callers to an application and protect it from
unexpected or forbidden agents. The filter supports configuration with either a safe-list (ALLOW) or
block-list (DENY) set of policies based off properties of the connection (IPs, ports, SSL subject)
as well as the incoming request's HTTP headers.

* :ref:`v2 API reference <envoy_api_msg_config.filter.http.rbac.v2.RBAC>`

Per-Route Configuration
-----------------------

The RBAC filter configuration can be overridden or disabled on a per-route basis by providing a
:ref:`RBACPerRoute <envoy_api_msg_config.filter.http.rbac.v2.RBACPerRoute>` configuration on
the virtual host, route, or weighted cluster.

Statistics
----------

The RBAC filter outputs statistics in the *http.<stat_prefix>.rbac.* namespace. The :ref:`stat
prefix <config_http_conn_man_stat_prefix>` comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total requests that were allowed access by the filter
  denied, Counter, Total requests that were denied access by the filter
