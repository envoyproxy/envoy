The match delegate filter (:ref:`ExtensionWithMatcher
<envoy_v3_api_msg_extensions.common.matching.v3.ExtensionWithMatcher>`) can now create the wrapped
(nested) filter lazily, only once the match tree resolves to a non-skip result for the stream. This
avoids the per-stream cost of constructing a filter that ends up skipped. When lazy creation is
enabled, the nested filter's access loggers only run when the nested filter is actually created, so
streams whose match tree resolves to a skip no longer emit the nested filter's access logs. This
behavior is opt-in and guarded by runtime feature
``envoy.reloadable_features.match_delegate_lazy_creation``, which is disabled by default; set it to
``true`` to enable lazy creation.
