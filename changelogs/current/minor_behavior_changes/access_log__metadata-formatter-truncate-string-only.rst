The length limit ``Z`` of the metadata command operators (:ref:`%DYNAMIC_METADATA()%
<config_access_log_format_dynamic_metadata>`, :ref:`%CLUSTER_METADATA()%
<config_access_log_format_cluster_metadata>`, :ref:`%UPSTREAM_METADATA()%
<config_access_log_format_upstream_host_metadata>` and the :ref:`%METADATA()%
<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` formatter extension) now only
truncates string values in typed output such as JSON access logs. Previously any non-structured
value was rendered as JSON and, when the limit applied, emitted as a truncated string, so a
numeric value of ``1234`` with a limit of 2 was logged as the string ``"12"``. Numbers and
booleans now keep their type and are logged in full, matching structs and lists, which were
already never truncated. Text access logs are unchanged: the rendered value is still truncated to
``Z`` characters. This behavioral change can be reverted by setting the runtime guard
``envoy.reloadable_features.metadata_formatter_only_truncate_string`` to ``false``.
