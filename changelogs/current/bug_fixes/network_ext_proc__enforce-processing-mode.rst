Fixed the :ref:`network external processing filter <config_network_filters_ext_proc>` accepting
:ref:`ProcessingResponse <envoy_v3_api_msg_service.network_ext_proc.v3.ProcessingResponse>` messages
carrying data for a direction configured as ``SKIP`` in
:ref:`processing_mode <envoy_v3_api_field_extensions.filters.network.ext_proc.v3.NetworkExternalProcessor.processing_mode>`,
or setting both ``read_data`` and ``write_data``. Such responses are now ignored and counted under
``spurious_msgs_received``. This change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.network_ext_proc_enforce_processing_mode`` to ``false``.
