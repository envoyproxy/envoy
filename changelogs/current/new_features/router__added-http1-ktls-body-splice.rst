Added an experimental HTTP/1.1 kTLS body-splice fast path, guarded by
``envoy.reloadable_features.http1_ktls_body_splice`` and disabled by default. When eligible, a
Content-Length request or response body is relayed with in-kernel ``splice()`` and exposed through
``cluster.<name>.http1_ktls_splice.{engaged,abandoned,completed,truncated}`` counters.
