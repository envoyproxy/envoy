load("@envoy_api//bazel:api_build_system.bzl", "api_go_grpc_library", "api_go_proto_library", "api_proto_library_internal")

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

api_proto_library_internal(
    name = "discovery",
    srcs = ["discovery.proto"],
    visibility = [":friends"],
    deps = ["//envoy/api/v3alpha/core:base"],
)

api_go_proto_library(
    name = "discovery",
    proto = ":discovery",
    deps = ["//envoy/api/v3alpha/core:base_go_proto"],
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

api_go_grpc_library(
    name = "eds",
    proto = ":eds",
    deps = [
        ":discovery_go_proto",
        "//envoy/api/v3alpha/core:address_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/core:health_check_go_proto",
        "//envoy/api/v3alpha/endpoint:endpoint_go_proto",
        "//envoy/type:percent_go_proto",
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

api_go_grpc_library(
    name = "cds",
    proto = ":cds",
    deps = [
        ":discovery_go_proto",
        ":eds_go_grpc",
        "//envoy/api/v3alpha/auth:cert_go_proto",
        "//envoy/api/v3alpha/cluster:circuit_breaker_go_proto",
        "//envoy/api/v3alpha/cluster:filter_go_proto",
        "//envoy/api/v3alpha/cluster:outlier_detection_go_proto",
        "//envoy/api/v3alpha/core:address_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/core:config_source_go_proto",
        "//envoy/api/v3alpha/core:health_check_go_proto",
        "//envoy/api/v3alpha/core:protocol_go_proto",
        "//envoy/api/v3alpha/endpoint:endpoint_go_proto",
        "//envoy/type:percent_go_proto",
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

api_go_grpc_library(
    name = "lds",
    proto = ":lds",
    deps = [
        ":discovery_go_proto",
        "//envoy/api/v3alpha/core:address_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/listener:listener_go_proto",
        "//envoy/api/v3alpha/listener:udp_listener_config_go_proto",
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

api_go_grpc_library(
    name = "rds",
    proto = ":rds",
    deps = [
        ":discovery_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/core:config_source_go_proto",
        "//envoy/api/v3alpha/route:route_go_proto",
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

api_go_grpc_library(
    name = "srds",
    proto = ":srds",
    deps = [
        ":discovery_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
    ],
)
