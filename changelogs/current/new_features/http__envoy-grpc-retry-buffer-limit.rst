Added :ref:`retry_buffer_limit_bytes
<envoy_v3_api_field_config.core.v3.GrpcService.EnvoyGrpc.retry_buffer_limit_bytes>` to the Envoy gRPC
client. It sets how many request bytes a stream buffers so that it can be retried. Without it, a
request or stream that sends more than 64 KiB is no longer retried, for example a gRPC access log
stream configured with ``grpc_stream_retry_policy``.
