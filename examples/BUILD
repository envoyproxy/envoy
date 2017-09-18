licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

filegroup(
    name = "configs",
    srcs = [
        "front-proxy/front-envoy.json",
        "front-proxy/service-envoy.json",
        "grpc-bridge/config/s2s-grpc-envoy.json",
        "grpc-bridge/config/s2s-python-envoy.json",
        "jaeger-tracing/front-envoy-jaeger.json",
        "jaeger-tracing/service1-envoy-jaeger.json",
        "jaeger-tracing/service2-envoy-jaeger.json",
        "zipkin-tracing/front-envoy-zipkin.json",
        "zipkin-tracing/service1-envoy-zipkin.json",
        "zipkin-tracing/service2-envoy-zipkin.json",
    ],
)
