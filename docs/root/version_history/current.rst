1.15.3 (Pending)
================

Changes
-------
* listener: fix crash when disabling or re-enabling listeners due to overload while processing LDS updates.
* udp: fixed issue in which receiving truncated UDP datagrams would cause Envoy to crash.
* proxy_proto: fixed a bug where network filters would not have the correct downstreamRemoteAddress() when accessed from the StreamInfo. This could result in incorrect enforcement of RBAC rules in the RBAC network filter (but not in the RBAC HTTP filter), or incorrect access log addresses from tcp_proxy.
* tls: fix read resumption after triggering buffer high-watermark and all remaining request/response bytes are stored in the SSL connection's internal buffers.
