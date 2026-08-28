The values of the ``envoy.reloadable_features.match_headers_individually``,
``envoy.reloadable_features.validate_upstream_headers``,
``envoy.reloadable_features.http2_include_cookies_in_limits`` and
``envoy.reloadable_features.http2_discard_host_header`` runtime features are now latched at
header-matcher or codec-connection construction time instead of being looked up on hot code
paths (per header field, per encoded request or per header match). Runtime overrides of these
flags now take effect for newly created connections and newly loaded configurations rather
than immediately for existing ones.
