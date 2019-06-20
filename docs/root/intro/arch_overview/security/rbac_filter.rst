.. _arch_overview_rbac:

Role Based Access Control
=========================

* :ref:`Network filter configuration <config_network_filters_rbac>`.
* :ref:`HTTP filter configuration <config_http_filters_rbac>`.

The Role Based Access Control (RBAC) filter checks if the incoming request is authorized or not.
Unlike external authorization, the check of RBAC filter happens in the Envoy process and is
based on a list of policies from the filter config.

The RBAC filter can be either configured as a :ref:`network filter <config_network_filters_rbac>`,
or as a :ref:`HTTP filter <config_http_filters_rbac>` or both. If the request is deemed unauthorized
by the network filter then the connection will be closed. If the request is deemed unauthorized by
the HTTP filter the request will be denied with 403 (Forbidden) response.

Policy
------

The RBAC filter checks the request based on a list of
:ref:`policies <envoy_api_field_config.rbac.v2.RBAC.policies>`. A policy consists of a list of
:ref:`permissions <envoy_api_msg_config.rbac.v2.Permission>` and
:ref:`principals <envoy_api_msg_config.rbac.v2.Principal>`. The permission specifies the actions of
the request, for example, the method and path of a HTTP request. The principal specifies the
downstream client identities of the request, for example, the URI SAN of the downstream client
certificate. A policy is matched if its permissions and principals are matched at the same time.

Shadow Policy
-------------

The filter can be configured with a
:ref:`shadow policy <envoy_api_field_config.filter.http.rbac.v2.RBAC.shadow_rules>` that doesn't
have any effect (i.e. not deny the request) but only emit stats and log the result. This is useful
for testing a rule before applying in production.
