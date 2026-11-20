The filter manager now buffers body data stopped at a filter that body data has already passed in
that filter's own buffer, instead of in the single buffer shared by the whole stream, so a filter
can stop and buffer again after letting data through without mixing its data into data buffered
further down the chain. For such a filter, ``decodingBuffer()``, ``addDecodedData()`` and
``modifyDecodingBuffer()`` (and their encoding counterparts) operate on its own buffer; for all
other filters they operate on the shared buffer as before. The stream buffer limit applies to the
total held by all buffers in each direction. This only takes effect when
``envoy.reloadable_features.filter_manager_per_filter_observed_end_stream`` is also enabled, and
can be reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_per_filter_buffer`` to ``false``.
