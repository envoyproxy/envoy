Added UDP and TCP header routing filters
(:ref:`envoy.filters.udp.session.header_routing
<envoy_v3_api_msg_extensions.filters.udp.udp_proxy.session.header_routing.v3.HeaderRouting>` and
:ref:`envoy.filters.network.header_routing
<envoy_v3_api_msg_extensions.filters.network.header_routing.v3.HeaderRouting>`) that parse and strip
a fixed 8-byte header (magic, version, room IP and room port) from the start of each UDP datagram /
TCP byte stream, and dynamically route the remaining payload to the room server encoded in the
header via the corresponding dynamic forward proxy filter.
