.. _config_http_filters_rbac:

Role Based Access Control (RBAC) Filter
=======================================

The RBAC filter is used to authorize actions by identified downstream clients. This is useful to
explicitly manage callers to an application and protect it from unexpected or forbidden agents. The
filter supports configuration with either a safe-list (ALLOW) or block-list (DENY) set of policies,
or a matcher with different actions, based off properties of the connection (IPs, ports, SSL subject)
as well as the incoming request's HTTP headers. This filter also supports policy in both enforcement
and shadow mode, shadow mode won't effect real users, it is used to test that a new set of policies
work before rolling out to production.

When a request is denied, the :ref:`RESPONSE_CODE_DETAILS<config_access_log_format_response_code_details>`
will include the name of the matched policy that caused the deny in the format of ``rbac_access_denied_matched_policy[policy_name]``
(policy_name will be ``none`` if no policy matched), this helps to distinguish the deny from Envoy RBAC
filter and the upstream backend.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBAC>`

Per-Route Configuration
-----------------------

The RBAC filter configuration can be overridden or disabled on a per-route basis by providing a
:ref:`RBACPerRoute <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBACPerRoute>` configuration on
the virtual host, route, or weighted cluster.

Statistics
----------

The RBAC filter outputs statistics in the *http.<stat_prefix>.rbac.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>` comes from the
owning HTTP connection manager.

For the shadow rule statistics ``shadow_allowed`` and ``shadow_denied``, the :ref:`shadow_rules_stat_prefix <envoy_v3_api_field_extensions.filters.http.rbac.v3.RBAC.shadow_rules_stat_prefix>`
can be used to add an extra prefix to output the statistics in the *http.<stat_prefix>.rbac.<shadow_rules_stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total requests that were allowed access
  denied, Counter, Total requests that were denied access
  shadow_allowed, Counter, Total requests that would be allowed access by the filter's shadow rules
  shadow_denied, Counter, Total requests that would be denied access by the filter's shadow rules
  logged, Counter, Total requests that should be logged
  not_logged, Counter, Total requests that should not be logged

.. _config_http_filters_rbac_dynamic_metadata:

Dynamic Metadata
----------------

The RBAC filter emits the following dynamic metadata.

For the shadow rules dynamic metadata ``shadow_effective_policy_id`` and ``shadow_engine_result``, the :ref:`shadow_rules_stat_prefix <envoy_v3_api_field_extensions.filters.http.rbac.v3.RBAC.shadow_rules_stat_prefix>`
can be used to add an extra prefix to the corresponding dynamic metadata key.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  shadow_effective_policy_id, string, The effective shadow policy ID matching the action (if any).
  shadow_engine_result, string, The engine result for the shadow rules (i.e. either ``allowed`` or ``denied``).
  access_log_hint, boolean, Whether the request should be logged. This metadata is shared and set under the key namespace 'envoy.common' (See :ref:`Shared Dynamic Metadata<shared_dynamic_metadata>`).
