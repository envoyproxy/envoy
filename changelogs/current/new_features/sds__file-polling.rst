Added :ref:`poll_interval
<envoy_v3_api_field_config.core.v3.PathConfigSource.poll_interval>` to filesystem configuration
sources. When used with SDS, Envoy polls both the SDS configuration and the same secret files that
are watched in event-based mode, allowing rotation when filesystem notifications are unreliable or
a custom deployment model does not generate the move or modification events handled by watching the
path or directory.
