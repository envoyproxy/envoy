.. _arch_overview_matching_listener:

Matching Filter Chains in Listeners
===================================

Envoy listeners implement the :ref:`matching API <envoy_v3_api_msg_.xds.type.matcher.v3.Matcher>` for selecting a filter
chain based on a collection of :ref:`network inputs <extension_category_envoy.matching.network.input>`. Matching is done
once per connection. Connections are drained when the associated named filter chain configuration changes, but not when
the filter chain matcher is the only updated field in a listener.

The action in the matcher API must be a string value corresponding to the name of the filter chain. If there is no
filter chain with the given name, the match fails, and the :ref:`default filter chain
<envoy_v3_api_field_config.listener.v3.Listener.default_filter_chain>` is used if specified, or the connection is
rejected. Filter chain matcher requires that all filter chains in a listener are uniquely named.

The matcher API replaces the existing filter :ref:`filter_chain_match
<envoy_v3_api_field_config.listener.v3.FilterChain.filter_chain_match>` field. When using the matcher API, the filter
chain match field is ignored and should not be set.

Examples
########

Detect TLS traffic
******************

The following examples uses :ref:`tls_inspector <config_listener_filters_tls_inspector>` listener filter to detect
whether the transport appears to be TLS, in which case the matcher in the listener selects the filter chain ``tls``.
Otherwise, the filter chain ``plaintext`` is used.

.. literalinclude:: _include/listener_tls.yaml
    :language: yaml
    :lines: 37-56
    :caption: :download:`listener_tls.yaml <_include/listener_tls.yaml>`

Match Against the Destination IP
********************************

The following example assumes :ref:`PROXY protocol <config_listener_filters_proxy_protocol>` is used for incoming
traffic. If the recovered destination IP is in CIDR ``10.0.0.0/24``, then the filter chain ``vip`` is used. Otherwise,
the filter chain ``default`` is used.

.. literalinclude:: _include/listener_vip.yaml
    :language: yaml
    :lines: 29-48
    :caption: :download:`listener_vip.yaml <_include/listener_vip.yaml>`

Match Against the Destination Port and the Source IP
****************************************************

The following example uses :ref:`original_dst <config_listener_filters_original_dst>` listener filter to recover the
original destination port. The matcher in the listener selects one of the three filter chains ``http``, ``internal``,
and ``tls`` as follows:

* If the destination port is ``80``, then the filter chain ``http`` accepts the connection.
* If the destination port is ``443`` and the source IP is in the range ``192.0.0.0/2`` or ``10.0.0.0/24``, then the
  filter chain ``internal`` accepts the connection. If the source IP is not in the ranges then the filter chain ``tls``
  accepts the connection.
* Otherwise, the connection is rejected, because there is no default filter chain.

.. literalinclude:: _include/listener_complicated.yaml
    :language: yaml
    :lines: 58-102
    :caption: :download:`listener_complicated.yaml <_include/listener_complicated.yaml>`
