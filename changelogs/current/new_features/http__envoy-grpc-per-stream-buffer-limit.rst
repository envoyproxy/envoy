Added :ref:`per_stream_buffer_limit_bytes
<envoy_v3_api_field_config.core.v3.GrpcService.EnvoyGrpc.per_stream_buffer_limit_bytes>` to the
Envoy gRPC client, matching the Google gRPC field of the same name. It sets the buffer limit of each
stream, which bounds both the request data buffered for retries and the data buffered before the
upstream is ready, above which the stream reports that it is above its write buffer high watermark.
Without it, the 64 KiB default applies, so a request or stream that sends more than 64 KiB before
the response headers is no longer retried, for example a gRPC access log stream configured with
``grpc_stream_retry_policy``.
