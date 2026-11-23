Added support for :ref:`process_new_connection
<envoy_v3_api_field_extensions.filters.network.ext_proc.v3.ProcessingMode.process_new_connection>`
event handling in the :ref:`network external processing filter <config_network_filters_ext_proc>`,
allowing external processors to receive :ref:`NewConnection
<envoy_v3_api_msg_service.network_ext_proc.v3.NewConnection>` metadata and attributes on connection
establishment and inject dynamic metadata or control connection lifecycle before payload streaming.
