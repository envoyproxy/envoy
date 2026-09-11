.. _config_udp_session_filters_dynamic_forward_proxy:

Dynamic forward proxy
==================================

Through the combination of a custom preceding filter that sets the ``envoy.upstream.dynamic_host`` and ``envoy.upstream.dynamic_port`` filter state
keys, and when used with the :ref:`dynamic forward proxy cluster <envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>`,
Envoy supports dynamic forward proxy for UDP flows. The implementation works just like the
:ref:`HTTP dynamic forward proxy <arch_overview_http_dynamic_forward_proxy>`, but using the value in
UDP session's filter state as target host and port instead.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.dynamic_forward_proxy.v3.FilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.session.dynamic_forward_proxy.v3.FilterConfig>`

.. _config_udp_session_filters_dynamic_forward_proxy_stats:

Statistics
----------

Every configured filter has statistics rooted at *udp.session.dynamic_forward_proxy.<stat_prefix>.*
with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  buffer_overflow, Counter, Number of datagrams dropped due to the filter buffer being overflowed
