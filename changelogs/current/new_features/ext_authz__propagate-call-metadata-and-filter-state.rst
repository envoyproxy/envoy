Added :ref:`propagate_call_metadata_namespaces
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.propagate_call_metadata_namespaces>`
and :ref:`propagate_call_filter_state_keys
<envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.propagate_call_filter_state_keys>`
to the external authorization filter. When set, the configured dynamic metadata namespaces and
FilterState keys produced on the authorization (Check) call's own stream, for example by a custom
load balancer on the callout cluster, are copied onto the downstream request after the call
completes, so downstream access logs and filters can observe them.
