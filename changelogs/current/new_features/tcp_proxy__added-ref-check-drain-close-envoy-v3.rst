Added :ref:`check_drain_close
<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.check_drain_close>` to the TCP proxy
filter to close downstream connections with ``FlushWrite`` when the drain manager requests drain close during
downstream read or write handling.
