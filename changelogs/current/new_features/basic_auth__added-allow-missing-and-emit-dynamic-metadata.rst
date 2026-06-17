Added ``allow_missing`` field to the BasicAuth HTTP filter. When set to ``true``, requests
with no ``Basic`` credentials (missing or non-Basic ``Authorization`` header) are passed through
instead of rejected, enabling OR-semantics when combining BasicAuth with other auth filters
(e.g. JWT). Invalid credentials are still rejected.
Added ``emit_dynamic_metadata`` field. When set to ``true``, the filter emits dynamic metadata
on successful authentication under the ``envoy.filters.http.basic_auth`` namespace with key
``username``, allowing downstream filters (e.g. RBAC) to act on BasicAuth success.
