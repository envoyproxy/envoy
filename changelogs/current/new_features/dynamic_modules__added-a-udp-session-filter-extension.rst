Added a dynamic modules UDP session filter extension
(``envoy.filters.udp.session.dynamic_modules``) that allows implementing UDP proxy session filters
in a dynamic module loaded via ``dlopen``. The filter bridges the read path
(``on_new_session``/``on_data``), the write path (``on_write``), and session completion to the
module ABI, and exposes datagram read/write, peer and local addresses, the session id, datagram
injection, dynamic metadata, and per-config and per-filter metrics. See
:ref:`DynamicModuleSessionFilter <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.session.dynamic_modules.v3.DynamicModuleSessionFilter>`
for configuration details.
