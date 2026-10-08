Fixes `CVE-2026-73550 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-qgf6-qvhw-4hvh>`_

Account for the length of dropped ``Host`` headers in HTTP/2 request header map size and count limits. ``Host`` headers
are dropped when they match HTTP/2 ```:authority`` header.

This behavioral change can be reverted by setting the runtime
guard ``envoy.reloadable_features.http2_track_size_of_dropped_host_header`` to ``false``.
