.. _config_network_filters_rbac:

Role Based Access Control (RBAC) Network Filter
===============================================

The RBAC network filter is used to authorize actions by identified downstream clients. This is useful
to explicitly manage callers to an application and protect it from unexpected or forbidden agents.
The filter supports configuration with either a safe-list (ALLOW) or block-list (DENY) set of policies,
or a matcher with different actions, based on properties of the connection (IPs, ports, SSL subject).
This filter also supports policy in both enforcement and shadow modes. Shadow mode won't effect real
users, it is used to test that a new set of policies work before rolling out to production.

When a request is denied, the :ref:`CONNECTION_TERMINATION_DETAILS<config_access_log_format_connection_termination_details>`
will include the name of the matched policy that caused the deny in the format of ``rbac_access_denied_matched_policy[policy_name]``
(policy_name will be ``none`` if no policy matched), this helps to distinguish the deny from Envoy
RBAC filter and the upstream backend.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.rbac.v3.RBAC>`

Statistics
----------

The RBAC network filter outputs statistics in the *<stat_prefix>.rbac.* namespace.

For the shadow rule statistics ``shadow_allowed`` and ``shadow_denied``, the :ref:`shadow_rules_stat_prefix <envoy_v3_api_field_extensions.filters.network.rbac.v3.RBAC.shadow_rules_stat_prefix>`
can be used to add an extra prefix to output the statistics in the *<stat_prefix>.rbac.<shadow_rules_stat_prefix>.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  allowed, Counter, Total requests that were allowed access
  denied, Counter, Total requests that were denied access
  shadow_allowed, Counter, Total requests that would be allowed access by the filter's shadow rules
  shadow_denied, Counter, Total requests that would be denied access by the filter's shadow rules
  logged, Counter, Total requests that should be logged
  not_logged, Counter, Total requests that should not be logged

.. _config_network_filters_rbac_tcp_proxy:

Usage with TCP Proxy
--------------------

When using the RBAC network filter with the :ref:`TCP proxy <config_network_filters_tcp_proxy>` filter,
the default behavior establishes upstream connections immediately when a downstream connection is accepted.
This means the upstream connection may be established before RBAC enforcement completes.

To ensure upstream connections are only established after RBAC allows the connection, configure the
TCP proxy filter to delay upstream connection establishment using
:ref:`upstream_connect_mode <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.upstream_connect_mode>`.

Example configuration:

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.rbac
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
        stat_prefix: tcp
        rules:
          policies:
            "require-mtls":
              permissions:
              - any: true
              principals:
              - authenticated:
                  principal_name:
                    exact: "spiffe://cluster.local/ns/default/sa/frontend"
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: backend
        upstream_connect_mode: ON_DOWNSTREAM_DATA
        max_early_data_bytes: 8192

In this configuration:

* ``upstream_connect_mode: ON_DOWNSTREAM_DATA`` delays the upstream connection until data is received
  from the downstream client.
* RBAC enforcement happens when data arrives, before the TCP proxy establishes the upstream connection.
* If RBAC denies the request, the connection is closed without ever connecting to the upstream.

Alternatively, use ``ON_DOWNSTREAM_TLS_HANDSHAKE`` to wait for the TLS handshake to complete, which
provides access to client certificates for RBAC policies that use :ref:`authenticated
<envoy_v3_api_field_config.rbac.v3.Principal.authenticated>` principals.

.. attention::

  The ``ON_DOWNSTREAM_DATA`` mode is not suitable for server-first protocols where the server sends
  the initial greeting (e.g., SMTP, MySQL, POP3). For such protocols, use the default ``IMMEDIATE``
  mode and accept that upstream connections may be established before RBAC enforcement.

.. _config_network_filters_rbac_dynamic_metadata:

Dynamic Metadata
----------------

The RBAC filter emits the following dynamic metadata.

For the shadow rules dynamic metadata ``shadow_effective_policy_id`` and ``shadow_engine_result``, the :ref:`shadow_rules_stat_prefix <envoy_v3_api_field_extensions.filters.network.rbac.v3.RBAC.shadow_rules_stat_prefix>`
can be used to add an extra prefix to the corresponding dynamic metadata key.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  shadow_effective_policy_id, string, The effective shadow policy ID matching the action (if any).
  shadow_engine_result, string, The engine result for the shadow rules (i.e. either ``allowed`` or ``denied``).
  access_log_hint, boolean, Whether the request should be logged. This metadata is shared and set under the key namespace 'envoy.common' (See :ref:`Shared Dynamic Metadata<shared_dynamic_metadata>`).
