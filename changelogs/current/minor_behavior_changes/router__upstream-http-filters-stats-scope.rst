The router's :ref:`upstream HTTP filter chain
<envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_http_filters>` is now built
under a stats scope that inherits the connection manager's stat prefix (``http.<stat_prefix>.``),
matching how the cluster-level upstream filter chain is scoped to ``cluster.<name>.``. Previously
the chain was built at the root scope, so stats emitted by upstream filters had no downstream
context. This behavior is guarded by runtime flag
``envoy.reloadable_features.router_upstream_filters_scoped_stat_prefix`` and can be reverted by
setting it to ``false``.
