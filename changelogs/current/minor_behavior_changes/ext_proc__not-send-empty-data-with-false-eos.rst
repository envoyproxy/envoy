ext_proc: Avoid sending empty data frames with end_stream=false to the external processing server.
The Envoy filter manager sometimes generates these frames, which Envoy consumes internally without
forwarding to the backend. Skipping these frames during external processing as well. This behavior
can be reverted by setting the runtime guard
``envoy.reloadable_features.ext_proc_not_send_empty_data_with_false_eos`` to ``false``.
