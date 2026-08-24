``metadata``: ``MetadataKey`` now supports accessing list elements via index. Path segments can
now specify either a struct field name (``key``) or a list element index (``index``). This allows
the ratelimit filter's ``metadata`` descriptor action to access values extracted by
``grpc_field_extraction`` (which writes ``ListValue``) without requiring a Lua bridge. For example,
to access the first element of a list at ``envoy.filters.http.grpc_field_extraction.tenant_id``,
use ``path: [{key: tenant_id}, {index: 0}]``. If the index is out of bounds or the value is not a
``ListValue``, the lookup returns an empty ``Value`` (same behavior as accessing a non-existent key).
