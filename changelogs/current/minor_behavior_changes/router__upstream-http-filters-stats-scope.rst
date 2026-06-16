The router's :ref:`upstream HTTP filter chain
<envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_http_filters>` is now built
under a stats scope that inherits the connection manager's stat prefix (``http.<stat_prefix>.``),
matching how the cluster-level upstream filter chain is scoped to ``cluster.<name>.``. Previously
the chain was built at the root scope, so stats emitted by upstream filters had no downstream
context.
