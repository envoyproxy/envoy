Fixed UDP proxy :ref:`proxy_host
<envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.UdpTunnelingConfig.proxy_host>`
to resolve ``%METADATA(CLUSTER:example:hostname)%`` from the cluster selected for the UDP session,
including when a session filter selects the cluster.
