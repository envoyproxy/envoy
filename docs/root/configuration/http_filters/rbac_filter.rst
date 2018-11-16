.. _config_http_filters_rbac:

Role Based Access Control (RBAC) Filter
=======================================

The RBAC filter is used to authorize actions (permissions) by identified downstream clients
(principals). This is useful to explicitly manage callers to an application and protect it from
unexpected or forbidden agents. The filter supports configuration with either a safe-list (ALLOW) or
block-list (DENY) set of policies based off properties of the connection (IPs, ports, SSL subject)
as well as the incoming request's HTTP headers. This filter also supports policy in both enforcement
and shadow mode, shadow mode won't effect real users, it is used to test that a new set of policies
work before rolling out to production.

* :ref:`v2 API reference <envoy_api_msg_config.filter.http.rbac.v2.RBAC>`

Per-Route Configuration
-----------------------

The RBAC filter configuration can be overridden or disabled on a per-route basis by providing a
:ref:`RBACPerRoute <envoy_api_msg_config.filter.http.rbac.v2.RBACPerRoute>` configuration on
the virtual host, route, or weighted cluster.

Statistics
----------

The RBAC filter outputs statistics in the *http.<stat_prefix>.rbac.* namespace. The :ref:`stat prefix
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stat_prefix>` comes from the
owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total requests that were allowed access
  denied, Counter, Total requests that were denied access
  shadow_allowed, Counter, Total requests that would be allowed access by the filter's shadow rules
  shadow_denied, Counter, Total requests that would be denied access by the filter's shadow rules

.. _config_http_filters_rbac_dynamic_metadata:

Dynamic Metadata
----------------

The RBAC filter emits the following dynamic metadata.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  shadow_effective_policy_id, string, The effective shadow policy ID matching the action (if any).
  shadow_engine_result, string, The engine result for the shadow rules (i.e. either `allowed` or `denied`).
