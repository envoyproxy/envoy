Fix `CVE-2026-73552 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-23xh-2qxr-3xv8>`_

Switch safe_regex charset mode from UTF-8 to Latin1. HTTP headers are not UTF-8 encoded
and must use Latin1 charset for regex expressions.
This behavioral change can be temporarily reverted by setting runtime
guard ``envoy.reloadable_features.re2_use_latin1_mode`` to ``false``.
