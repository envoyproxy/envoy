Fix: `CVE-2026-73546 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-pv9h-4fxf-7vrg>`_

Sanitize stat names before converting them to HTML.
The change is guarded by runtime guard ``envoy.reloadable_features.sanitize_html_stats_names``.
