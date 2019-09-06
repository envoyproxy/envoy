load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

# Friends of core API packages - filters, services, service configs.
# Package //envoy/api/v3alpha contains xDS and discovery definitions that should
# be in //envoy/service/discovery, but remain here for backwards compatibility.
package_group(
    name = "friends",
    packages = [
        "//envoy/admin/...",
        "//envoy/api/v3alpha",
        "//envoy/config/...",
        "//envoy/data/...",
        "//envoy/service/...",
    ],
)

api_proto_package(
    name = "v3alpha",
    has_services = True,
    deps = [
        "//envoy/api/v3alpha/auth",
        "//envoy/api/v3alpha/cluster",
        "//envoy/api/v3alpha/core",
        "//envoy/api/v3alpha/endpoint:pkg",
        "//envoy/api/v3alpha/listener:pkg",
        "//envoy/api/v3alpha/ratelimit:pkg",
        "//envoy/api/v3alpha/route:pkg",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "discovery",
    srcs = ["discovery.proto"],
    visibility = [":friends"],
    deps = ["//envoy/api/v3alpha/core:base"],
)

api_proto_library_internal(
    name = "eds",
    srcs = ["eds.proto"],
    has_services = 1,
    visibility = [":friends"],
    deps = [
        ":discovery",
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:health_check",
        "//envoy/api/v3alpha/endpoint",
        "//envoy/type:percent",
    ],
)

api_proto_library_internal(
    name = "cds",
    srcs = ["cds.proto"],
    has_services = 1,
    visibility = [":friends"],
    deps = [
        ":discovery",
        ":eds",
        "//envoy/api/v3alpha/auth:cert",
        "//envoy/api/v3alpha/cluster:circuit_breaker",
        "//envoy/api/v3alpha/cluster:filter",
        "//envoy/api/v3alpha/cluster:outlier_detection",
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
        "//envoy/api/v3alpha/core:health_check",
        "//envoy/api/v3alpha/core:protocol",
        "//envoy/api/v3alpha/endpoint",
        "//envoy/type:percent",
    ],
)

api_proto_library_internal(
    name = "lds",
    srcs = ["lds.proto"],
    has_services = 1,
    visibility = [":friends"],
    deps = [
        ":discovery",
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/listener",
        "//envoy/api/v3alpha/listener:udp_listener_config",
    ],
)

api_proto_library_internal(
    name = "rds",
    srcs = ["rds.proto"],
    has_services = 1,
    visibility = [":friends"],
    deps = [
        ":discovery",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
        "//envoy/api/v3alpha/route",
    ],
)

api_proto_library_internal(
    name = "srds",
    srcs = ["srds.proto"],
    has_services = 1,
    visibility = [":friends"],
    deps = [
        ":discovery",
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/route",
    ],
)
