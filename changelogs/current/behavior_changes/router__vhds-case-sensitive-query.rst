Added runtime flag ``envoy.reloadable_features.vhds_case_sensitive_query`` to control case sensitivity for VHDS
(Virtual Host Discovery Service) queries. When enabled (default is ``true``), VHDS queries use the original
case-sensitive host header value. When disabled, host headers are lowercased before VHDS query for backwards
compatibility. Note: Domain matching is always case-insensitive regardless of this flag.
See :ref:`VHDS <config_http_conn_man_vhds>` for more details.
