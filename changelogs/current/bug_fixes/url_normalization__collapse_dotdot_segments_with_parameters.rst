Fixes `CVE-2026-73551 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-2w8w-rfw7-8gg4>`_

Strip URL path parameters from dot and dotdot segments (segments that start with ``/..;`` or ``/.;``). This allows
path canonicalization to interpret them correctly. Stripping of path parameters from dot and dotdot segments
occurs only if the ``normalize_path`` configuration option is enabled.

This behavioral change can be temporarily reverted by setting runtime
guard ``envoy.reloadable_features.strip_dotdot_segments_with_parameters`` to ``false``.
