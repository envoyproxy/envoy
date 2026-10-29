Fixed UDP listeners enabling the ``UDP_GRO`` socket option when
:ref:`prefer_gro <envoy_v3_api_field_config.core.v3.UdpSocketConfig.prefer_gro>` is false or unset.
Coalesced datagrams could previously be forwarded as a single datagram or dropped as truncated.
