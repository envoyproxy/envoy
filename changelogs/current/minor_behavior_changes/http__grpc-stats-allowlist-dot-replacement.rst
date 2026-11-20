The gRPC statistics filter now applies
:ref:`replace_dots_in_grpc_service_name
<envoy_v3_api_field_extensions.filters.http.grpc_stats.v3.FilterConfig.replace_dots_in_grpc_service_name>`
to allowlisted methods. Previously, this option only affected statistics emitted with
``stats_for_all_methods`` set to ``true``. When enabled with ``individual_method_stats_allowlist``,
dots in service names are now replaced with underscores in emitted metric names. Allowlist entries
continue to use the original service names, including dots. Dashboards and alerts that refer to the
previous metric names need to be updated.
