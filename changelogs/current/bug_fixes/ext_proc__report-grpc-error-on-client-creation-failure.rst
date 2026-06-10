Fixed a bug in the
:ref:`ext_proc <envoy_v3_api_msg_extensions.filters.http.ext_proc.v3.ExternalProcessor>`
HTTP filter where a failure to create the gRPC client for the external processor caused
``Filter::openStream()`` to short-circuit to ``IgnoreError`` regardless of
:ref:`failure_mode_allow
<envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExternalProcessor.failure_mode_allow>`.
The underlying
``ProcessorClientImpl::start()`` returned ``nullptr`` without invoking
``onGrpcError``/``onGrpcClose``, so the consuming filter could not distinguish a
client-creation failure from a successful no-op. The client now reports the failure via
``onGrpcError`` with status ``INTERNAL`` so the consuming filter applies the configured
failure_mode_allow semantic. This behavior can be temporarily reverted by setting
``envoy.reloadable_features.ext_proc_report_client_creation_error`` to ``false``.
