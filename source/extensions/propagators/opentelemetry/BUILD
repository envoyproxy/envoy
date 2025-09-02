load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "propagator_interface_lib",
    srcs = [
        "propagator.cc",
    ],
    hdrs = [
        "propagator.h",
    ],
    deps = [
        "//source/common/common:logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/http:header_map_lib",
        "//source/common/tracing:trace_context_lib",
        "//source/extensions/tracers/opentelemetry:span_context_lib",
        "@io_opentelemetry_cpp//api",
    ],
)

envoy_cc_library(
    name = "propagator_lib",
    srcs = [
        "propagator_factory.cc",
    ],
    hdrs = [
        "propagator_factory.h",
    ],
    deps = [
        ":propagator_interface_lib",
        "//envoy/api:api_interface",
        "//source/common/common:utility_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/propagators/opentelemetry/b3:b3_propagator_lib",
        "//source/extensions/propagators/opentelemetry/w3c:w3c_propagators_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)
