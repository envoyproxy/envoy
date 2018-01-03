licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

filegroup(
    name = "configs",
    srcs = [
        "front-proxy/deprecated_v1/front-envoy-v1.json",
        "front-proxy/deprecated_v1/service-envoy-v1.json",
        "front-proxy/front-envoy.json",
        "front-proxy/service-envoy.json",
        "grpc-bridge/config/s2s-grpc-envoy.json",
        "grpc-bridge/config/s2s-python-envoy.json",
        "grpc-bridge/deprecated_v1/s2s-grpc-envoy-v1.json",
        "grpc-bridge/deprecated_v1/s2s-python-envoy-v1.json",
        "jaeger-tracing/deprecated_v1/front-envoy-jaeger-v1.json",
        "jaeger-tracing/deprecated_v1/service1-envoy-jaeger-v1.json",
        "jaeger-tracing/deprecated_v1/service2-envoy-jaeger-v1.json",
        "jaeger-tracing/front-envoy-jaeger.json",
        "jaeger-tracing/service1-envoy-jaeger.json",
        "jaeger-tracing/service2-envoy-jaeger.json",
        "zipkin-tracing/deprecated_v1/front-envoy-zipkin-v1.json",
        "zipkin-tracing/deprecated_v1/service1-envoy-zipkin-v1.json",
        "zipkin-tracing/deprecated_v1/service2-envoy-zipkin-v1.json",
        "zipkin-tracing/front-envoy-zipkin.json",
        "zipkin-tracing/service1-envoy-zipkin.json",
        "zipkin-tracing/service2-envoy-zipkin.json",
    ],
)
