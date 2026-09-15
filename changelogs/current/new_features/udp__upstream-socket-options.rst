Added support for applying :ref:`socket options
<envoy_v3_api_field_config.core.v3.BindConfig.socket_options>` configured in a cluster's
:ref:`upstream_bind_config <envoy_v3_api_field_config.cluster.v3.Cluster.upstream_bind_config>` to
UDP proxy upstream sockets. This supports use cases such as packet marking with Linux ``SO_MARK``.
:ref:`use_original_src_ip
<envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.use_original_src_ip>` is
compatible with cluster socket options, which are combined with the transparent socket options.
