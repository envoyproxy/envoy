.. _config_network_filters_rbac:

Role Based Access Control (RBAC) Network Filter
===============================================

The RBAC network filter is used to authorize actions (permissions) by identified downstream clients
(principals). This is useful to explicitly manage callers to an application and protect it from
unexpected or forbidden agents. The filter supports configuration with either a safe-list (ALLOW) or
block-list (DENY) set of policies based on properties of the connection (IPs, ports, SSL subject).
This filter also supports policy in both enforcement and shadow modes. Shadow mode won't effect real
users, it is used to test that a new set of policies work before rolling out to production.

* :ref:`v2 API reference <envoy_api_msg_config.filter.network.rbac.v2.RBAC>`

Statistics
----------

The RBAC network filter outputs statistics in the *<stat_prefix>.rbac.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total requests that were allowed access
  denied, Counter, Total requests that were denied access
  shadow_allowed, Counter, Total requests that would be allowed access by the filter's shadow rules
  shadow_denied, Counter, Total requests that would be denied access by the filter's shadow rules
