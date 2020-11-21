1.15.3 (Pending)
================

Changes
-------
* listener: fix crash when disabling or re-enabling listeners due to overload while processing LDS updates.
* proxy_proto: fixed a bug where network filters would not have the correct downstreamRemoteAddress() when accessed from the StreamInfo. This could result in incorrect enforcement of RBAC rules in the RBAC network filter (but not in the RBAC HTTP filter), or incorrect access log addresses from tcp_proxy.
