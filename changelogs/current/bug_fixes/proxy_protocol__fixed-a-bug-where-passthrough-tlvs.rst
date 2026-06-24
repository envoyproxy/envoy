Fix: `CVE-2026-47692 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-wh36-hm39-mm3r>`_

Fixed a bug where passthrough TLVs combined with added TLVs could exceed the maximum length,
resulting in a mismatch between the size reported in the header and the number of bytes written.
This could allow a smuggled request from the host writing the PROXY protocol header to the upstream
host. This behavioral change can be reverted by setting the runtime guard
``envoy.reloadable_features.proxy_protocol_remove_too_long_tlvs`` to ``false``.
