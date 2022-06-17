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

The RBAC filter's rules can be either configured with a list of
:ref:`policies <envoy_v3_api_field_config.rbac.v3.RBAC.policies>` or the
:ref:`matching API <envoy_v3_api_msg_.xds.type.matcher.v3.Matcher>`.

Policy
------

The RBAC filter checks the request based on a list of
:ref:`policies <envoy_v3_api_field_config.rbac.v3.RBAC.policies>`. A policy consists of a list of
:ref:`permissions <envoy_v3_api_msg_config.rbac.v3.Permission>` and
:ref:`principals <envoy_v3_api_msg_config.rbac.v3.Principal>`. The permission specifies the actions of
the request, for example, the method and path of a HTTP request. The principal specifies the
downstream client identities of the request, for example, the URI SAN of the downstream client
certificate. A policy is matched if its permissions and principals are matched at the same time.

.. _arch_overview_rbac_matcher:

Matcher
-------
Instead of specifying :ref:`policies <envoy_v3_api_field_config.rbac.v3.RBAC.policies>`, the RBAC
filter can also be configured with the :ref:`matching API <envoy_v3_api_msg_.xds.type.matcher.v3.Matcher>`.
:ref:`Network inputs <extension_category_envoy.matching.network.input>` are available for both RBAC
network filter and HTTP filter, and :ref:`HTTP inputs <extension_category_envoy.matching.http.input>`
are only available in HTTP filter.

:ref:`RBAC matcher extensions <api-v3_config_rbac_matchers>` are not compatible with the
:ref:`matching API <envoy_v3_api_msg_.xds.type.matcher.v3.Matcher>`.

Shadow Policy and Shadow Matcher
--------------------------------

The filter can be configured with a
:ref:`shadow policy <envoy_v3_api_field_extensions.filters.http.rbac.v3.RBAC.shadow_rules>` or a
:ref:`shadow matcher <envoy_v3_api_field_extensions.filters.http.rbac.v3.RBAC.shadow_matcher>` that
doesn't have any effect (i.e. not deny the request) but only emit stats and log the result. This is
useful for testing a rule before applying in production.

.. _arch_overview_condition:

Condition
---------

In addition to the pre-defined permissions and principals, a policy may optionally provide an
authorization condition written in the `Common Expression Language
<https://github.com/google/cel-spec/blob/master/doc/intro.md>`_. The condition specifies an extra
clause that must be satisfied for the policy to match. For example, the following condition checks
whether the request path starts with ``/v1/``:

.. code-block:: yaml

  call_expr:
    function: startsWith
    args:
    - select_expr:
       operand:
         ident_expr:
           name: request
       field: path
    - const_expr:
       string_value: /v1/

Envoy provides a number of :ref:`request attributes <arch_overview_request_attributes>`
for expressive policies. Most attributes are optional and provide the default
value based on the type of the attribute. CEL supports presence checks for
attributes and maps using ``has()`` syntax, e.g. ``has(request.referer)``.
