Added :ref:`per_connection_buffer_limit_bytes
<envoy_v3_api_field_config.bootstrap.v3.Admin.per_connection_buffer_limit_bytes>` to the admin
interface. When set and the ``envoy.reloadable_features.admin_filter_yield_between_chunks``
runtime guard is enabled, admin responses produced in multiple chunks, such as ``/stats``, are flow
controlled: the next chunk is produced only once the client has drained the response buffered so
far, so a slow client no longer makes Envoy buffer the whole response. Responses produced in a
single chunk, such as ``/config_dump``, are not affected. If not set, there is no limit, as before.
