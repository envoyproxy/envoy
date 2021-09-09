.. _config_network_filters_sip_proxy:

Sip proxy
============

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.sip_proxy.v3alpha.SipProxy>`
* This filter should be configured with the name *envoy.filters.network.sip_proxy*.

Cluster Protocol Options
------------------------

Sip connections to upstream hosts can be configured by adding an entry to the appropriate
Cluster's :ref:`extension_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`
keyed by ``envoy.filters.network.sip_proxy``. The
:ref:`SipProtocolOptions<envoy_v3_api_msg_extensions.filters.network.sip_proxy.v3alpha.SipProtocolOptions>`
message describes the available options.

