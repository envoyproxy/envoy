Fixed a request/response body data-loss bug in the HTTP filter manager. When a filter stopped
iteration on headers (for example a wasm filter with ``allow_on_headers_stop_iteration``, which maps
to a single-iteration stop rather than ``StopAllIterationAndWatermark``), resumed asynchronously, and
then on a subsequent body frame moved that frame into the filter-manager buffer via
``addDecodedData()``/``addEncodedData()`` before returning ``Continue``, the now-empty frame was
forwarded down the chain and the buffered bytes were silently dropped. This corrupted large streamed
request bodies (for example one 16 KiB chunk lost when chained with an ``ext_proc`` filter in
``FULL_DUPLEX_STREAMED`` mode). The just-buffered data is now forwarded instead of the empty frame.
This behavioral change can be reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_forward_added_data_on_continue`` to ``false``.
