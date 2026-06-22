Fix: `CVE-2026-73553 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-77x5-xqjg-hprq>`_

RBAC path matching (via ``PathMatcher`` and ``UriTemplateMatcher``) now respects the route's
``ignore_path_parameters_in_path_matching`` configuration. When enabled on a route, the RBAC filter
will strip path parameters (everything after a semicolon in each path segment, e.g., transforming
``/admin;x=y/action;foo=bar`` to ``/admin/action``) before evaluating the path match. This ensures
path matching consistency between the Router and the RBAC filter, preventing authorization bypasses
where an attacker could append path parameters to bypass RBAC rules while still being routed to the
protected endpoint.

This behavioral change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.rbac_respect_ignore_path_parameters`` to ``false``.
