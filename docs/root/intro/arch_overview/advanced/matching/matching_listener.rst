.. _arch_overview_matching_listener:

Matching filter chains in listeners
===================================

Envoy listeners implement the :ref:`matching API <envoy_v3_api_msg_.xds.type.matcher.v3.Matcher>` for selecting a filter
chain based on a collection of :ref:`network inputs <_extension_category_envoy.matching.network.input>`. Matching is
done once per connection. Connections are drained when the associated named filter chain configuration changes, but not
when the filter chain matcher is the only updated field in a listener.

The action in the matcher API must be a string value corresponding to the name of the filter chain. If there is no
filter chain with the given name, the match fails, and the :ref:`default filter chain
<envoy_v3_api_field_config.listener.v3.Listener.default_filter_chain>` is used if specified, or the connection is
rejected. Filter chain matcher requires that all filter chains in a listener are uniquely named.

The matcher API replaces the existing filter :ref:`filter_chain_match
<envoy_v3_api_field_config.listener.v3.FilterChain.filter_chain_match>` field. When using the matcher API, the filter
chain match field is ignored and should not be set.

Examples
########

Multiple conditions
*******************

* if the destination port is 80, then the filter chain "http" is selected;
* if the destination port is 443 and the source IP is in the range 192.0.0.0/2, then the filter chain "internal" is selected;
* otherwise, if the destination port is 443, then the filter chain "https" is selected;
* otherwise, the default filter chain is selected (or the connection is rejected without the default filter chain).

.. literalinclude:: _include/listener-complicated.yaml
    :language: yaml
