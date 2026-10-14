Added the ``metadata_dropped`` HTTP/2 :ref:`codec stat <config_http_conn_man_stats_per_codec>`. It counts outbound
metadata that is dropped because :ref:`allow_metadata
<envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.allow_metadata>` isn't enabled on the connection.
