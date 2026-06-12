``HeaderMatcher`` when matching a header that has multiple values previously matched against the
combined string, e.g. ``x-role: user`` and ``x-role: admin`` would be matched against ``user,admin``.
A rule matching ``admin`` would not have matched. Such headers are now matched individually, so a
rule matching ``admin`` matches, and a rule matching ``user,admin`` no longer does. This only
affects headers sent as separate entries; a single header line containing ``user,admin`` is still
matched as one value. The change applies everywhere ``HeaderMatcher`` is used (route matching,
virtual clusters, rate limits, retry policies, access log filters, health checks, fault injection,
JWT auth, OAuth2, Thrift/Dubbo routing, etc.), completing the fix previously applied to RBAC rules
(CVE-2026-26308).

It does not change header matching performed via CEL expressions (e.g. RBAC
``condition``) or via the generic matching API's header inputs (e.g. ``HttpRequestHeaderMatchInput``),
which still observe the comma-concatenated value.

This behavioral change can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.match_headers_individually`` to ``false``. The recommended action is
to reconfigure undesirably impacted matchers to fit the new pattern.
