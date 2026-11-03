Added :ref:`dry_run <envoy_v3_api_field_extensions.filters.http.fault.v3.FaultAbort.dry_run>` to the
HTTP fault injection filter to support dry-run abort evaluation. When enabled, requests matching an
abort fault increment the ``aborts_dry_run`` :ref:`counter <config_http_filters_fault_injection_stats>`
and record configured :ref:`filter_metadata
<envoy_v3_api_field_extensions.filters.http.fault.v3.HTTPFault.filter_metadata>` in dynamic metadata
without aborting the request, while any configured delay or response rate limit is still injected.
