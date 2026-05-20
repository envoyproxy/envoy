Added :ref:`observability_name <envoy_v3_api_field_config.endpoint.v3.Endpoint.observability_name>` to
:ref:`Endpoint <envoy_v3_api_msg_config.endpoint.v3.Endpoint>` so endpoints can override the
observability name used in per-endpoint stats. This allows clusters with duplicate
endpoint addresses to expose distinct per-endpoint stats.
