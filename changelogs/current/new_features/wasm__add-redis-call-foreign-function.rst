Added a ``redis_call`` foreign function for the WASM extension, letting a WASM plugin issue a
Redis command (RESP2) against an existing cluster -- directly, or via an ``envoy_internal`` address
naming an internal listener, e.g. a ``redis_proxy`` network filter -- and receive the result
asynchronously via the existing proxy-wasm shared-queue mechanism. See :ref:`Redis calls
<arch_overview_wasm_redis_call>` for the full request/result contract, the command denylist, and
the per-plugin-instance concurrency and byte-size limits.
