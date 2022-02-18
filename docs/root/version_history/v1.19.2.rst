1.19.2 (Pending)
=====================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* dns_filter: :ref:`dns_filter <envoy_v3_api_msg_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig>`
  protobuf fields have been renumbered to restore compatibility with Envoy
  1.18, breaking compatibility with Envoy 1.19.0 and 1.19.1. The new field
  numbering allows control planes supporting Envoy 1.18 to gracefully upgrade to
  :ref:`dns_resolution_config <envoy_v3_api_field_extensions.filters.udp.dns_filter.v3alpha.DnsFilterConfig.ClientContextConfig.dns_resolution_config>`,
  provided they skip over Envoy 1.19.0 and 1.19.1.
  Control planes upgrading from Envoy 1.19.0 and 1.19.1 will need to
  vendor the corresponding protobuf definitions to ensure that the
  renumbered fields have the types expected by those releases.

Minor Behavior Changes
----------------------

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* listener: fixed an issue on Windows where connections are not handled by all worker threads.

Removed Config or Runtime
-------------------------

New Features
------------

Deprecated
----------
