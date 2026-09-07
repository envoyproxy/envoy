Fixes `CVE-2026-73511 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-m745-gh6x-349x>`_

Strip path parameters from individual path segments per https://datatracker.ietf.org/doc/html/rfc3986#section-3.3

This behavioral change can be temporarily reverted by setting runtime
guard ``envoy.reloadable_features.strip_path_parameters_per_segment`` to ``false``.
