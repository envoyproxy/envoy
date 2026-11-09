The ext_authz gRPC client now treats a response containing ``error_response`` as an error even when
the top-level ``status`` field is omitted and therefore defaults to ``OK`` in proto3.
