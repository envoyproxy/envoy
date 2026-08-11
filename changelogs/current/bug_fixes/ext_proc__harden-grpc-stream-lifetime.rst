Fixed multiple lifetime bugs in the external processing (``ext_proc``) filter and the underlying
gRPC async client that could lead to use-after-free or double delivery of callbacks. The gRPC
:ref:`async client <envoy_v3_api_msg_config.core.v3.GrpcService>` now holds an optional reference to
its stream callbacks and drops it once the stream is cleaned up or the owner detaches via
``waitForRemoteCloseAndDelete()``, so a stream that outlives its callbacks (for example while awaiting
remote close) no longer invokes callbacks on freed memory. Re-entrant resets during stream
initialization are guarded so remote close is not notified (and the tracing span not finished) twice
when the cluster is missing or stream creation fails synchronously, and half-close/cleanup no longer
dereference a stream that was never established. The ``ext_proc`` ``ThreadLocalStreamManager`` and
``ProcessorStreamImpl`` now close any still-open streams on destruction to avoid dangling references
into the underlying gRPC stream.
