.. _config_network_filters_thrift_proxy:

Thrift proxy
============

* :ref:`v2 API reference <envoy_api_msg_config.filter.network.thrift_proxy.v2alpha1.ThriftProxy>`

Cluster Protocol Options
------------------------

Thrift connections to upstream hosts can be configured by adding an entry to the appropriate
Cluster's :ref:`extension_protocol_options<envoy_api_field_Cluster.extension_protocol_options>`
keyed by `envoy.filters.network.thrift_proxy`. The
:ref:`ThriftProtocolOptions<envoy_api_msg_config.filter.network.thrift_proxy.v2alpha1.ThriftProtocolOptions>`
message describes the available options.
