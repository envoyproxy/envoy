Fix: `CVE-2026-47774 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-22m2-hvr2-xqc8>`_

HTTP/2 streams will now be reset if the stream violates the maximum header list size configured via
``mutable_max_request_headers_kb``. Note that this is different than the per header size specified
by ``max_header_field_size_kb``. Uncompressed cookies now count towards this limit to
protect Envoys against large uncompressed cookies causing excessive memory usage. Additionally, cookies
now also count towards ``max_headers_count`` limits.
This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.http2_include_cookies_in_limits`` to false.
