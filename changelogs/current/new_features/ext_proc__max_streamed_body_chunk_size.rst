Added :ref:`max_streamed_body_chunk_size
<envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExternalProcessor.max_streamed_body_chunk_size>`
and per-route :ref:`max_streamed_body_chunk_size
<envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExtProcOverrides.max_streamed_body_chunk_size>`
to limit the body payload size of individual ``ProcessingRequest`` messages sent in ``STREAMED`` and
``FULL_DUPLEX_STREAMED`` body processing modes.
