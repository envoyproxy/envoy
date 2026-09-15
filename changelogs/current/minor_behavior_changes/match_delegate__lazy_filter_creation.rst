The match delegate filter (:ref:`ExtensionWithMatcher
<envoy_v3_api_msg_extensions.common.matching.v3.ExtensionWithMatcher>`) can now create the wrapped
(nested) filter lazily, only once the match tree resolves to a non-skip result for the stream. This
avoids the per-stream cost of constructing a filter that ends up skipped. When lazy creation is
enabled and the skip decision is made from request headers or trailers, the nested filter is never
created and its access loggers do not run. When the match tree requires response data to resolve
(e.g. response-header matchers), the nested filter is created during the decode phase and its access
loggers run at stream end, since the filter participated in decoding. This behavior is opt-in and
guarded by runtime feature ``envoy.reloadable_features.match_delegate_lazy_creation``, which is
disabled by default; set it to ``true`` to enable lazy creation.
