.. _config_network_filters_forward_original_sni:

Forward Original SNI
=========================

The `forward_original_sni` is a network filter that instructs other filters,
such as `tcp_proxy`, to forward the SNI value from the downstream connection
to the upstream connection. The filter will do nothing for non-TLS connections or
for TLS connections without SNI.

This filter has no configuration. It must be installed before the
:ref:`tcp_proxy <config_network_filters_tcp_proxy>` filter.

* :ref:`v2 API reference <envoy_api_field_listener.Filter.name>`
