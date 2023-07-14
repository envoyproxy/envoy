load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements binary gRPC to JSON transcoding
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/grpc_json_transcoder_filter

envoy_extension_package()

envoy_cc_extension(
    name = "config",
)
