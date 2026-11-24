Added the :ref:`network fault injection filter <config_network_filters_fault_injection>`
(``envoy.filters.network.fault``) supporting fractional connection
:ref:`abort <envoy_v3_api_field_extensions.filters.network.fault.v3.NetworkFault.abort>`,
:ref:`dry_run <envoy_v3_api_field_extensions.filters.network.fault.v3.FaultAbort.dry_run>` mode
(recording ``aborts_dry_run`` stats and merging ``{"dry_run_abort": true}`` dynamic metadata
instead of writing user-configured ``filter_metadata``), runtime percentage overrides, and dynamic
:ref:`filter_metadata <envoy_v3_api_field_extensions.filters.network.fault.v3.NetworkFault.filter_metadata>`
for L4 TCP connections.
