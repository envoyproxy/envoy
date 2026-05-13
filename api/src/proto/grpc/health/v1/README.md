Vendored health.proto for buf linting
-------------------------------------

The `health.proto` file is copied and vendored here from
https://github.com/grpc/grpc/blob/master/src/proto/grpc/health/v1/health.proto.
This provides the matching `health.proto` to buf linting but strictly not to
the bazel build, which uses protobuf files from @grpc.

The buf linter, https://buf.build/docs/, uses
https://github.com/grpc/grpc-proto/blob/master/grpc/health/v1/health.proto. However
this has a different import path, `grpc/health/v1/health.proto` compared to
`src/proto/grpc/health/v1/health.proto`. Therefore by vendoring the `health.proto`
we can avoid patching gRPC BUILD definitions and maintain correct protobuf linting.

Note this file should only be required in the envoy repository for buf linting and
not for any builds.
