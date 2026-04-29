load("@io_bazel_rules_go//go:def.bzl", "go_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dynamic_modules_lib",
    srcs = ["dynamic_modules.cc"],
    hdrs = [
        "dynamic_modules.h",
    ],
    deps = [
        "//envoy/common:exception_lib",
        "//source/common/common:utility_lib",
        "//source/extensions/dynamic_modules/abi",
    ],
)

envoy_cc_library(
    name = "background_fetch_manager_lib",
    srcs = ["background_fetch_manager.cc"],
    hdrs = ["background_fetch_manager.h"],
    deps = [
        ":dynamic_modules_lib",
        "//envoy/singleton:manager_interface",
        "//source/common/config:remote_data_fetcher_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":dynamic_modules_lib",
        "//envoy/server:factory_context_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/synchronization",
    ],
    alwayslink = True,
)

# The shared package is pure Go (no cgo) — interfaces, types, and EmptyXxx no-ops shared
# across the SDK and module code. Modules depend on this transitively via go_sdk and
# go_sdk_abi.
go_library(
    name = "go_sdk_shared",
    srcs = [
        "sdk/go/shared/access_log.go",
        "sdk/go/shared/bootstrap.go",
        "sdk/go/shared/cert_validator.go",
        "sdk/go/shared/cluster.go",
        "sdk/go/shared/dns_resolver.go",
        "sdk/go/shared/http.go",
        "sdk/go/shared/listener_api.go",
        "sdk/go/shared/listener_base.go",
        "sdk/go/shared/load_balancer.go",
        "sdk/go/shared/matcher.go",
        "sdk/go/shared/network_api.go",
        "sdk/go/shared/network_base.go",
        "sdk/go/shared/program.go",
        "sdk/go/shared/tracer.go",
        "sdk/go/shared/transport_socket.go",
        "sdk/go/shared/types.go",
        "sdk/go/shared/udp_listener_api.go",
        "sdk/go/shared/udp_listener_base.go",
        "sdk/go/shared/upstream_http_tcp_bridge.go",
    ],
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared",
    visibility = ["//visibility:public"],
)

# The top-level sdk package — pure Go. Holds the factory registries and program-handle
# indirection. Imports shared.
go_library(
    name = "go_sdk",
    srcs = ["sdk/go/sdk.go"],
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go",
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/dynamic_modules:go_sdk_shared",
    ],
)

# The abi package — cgo-heavy. Implements the C-callable trampolines that Envoy invokes
# and the Go-callable wrappers that bridge into Envoy's host callbacks.
go_library(
    name = "go_sdk_abi",
    srcs = [
        "sdk/go/abi/access_log.go",
        "sdk/go/abi/bootstrap.go",
        "sdk/go/abi/cert_validator.go",
        "sdk/go/abi/cluster.go",
        "sdk/go/abi/dns_resolver.go",
        "sdk/go/abi/http.go",
        "sdk/go/abi/listener.go",
        "sdk/go/abi/load_balancer.go",
        "sdk/go/abi/matcher.go",
        "sdk/go/abi/network.go",
        "sdk/go/abi/program.go",
        "sdk/go/abi/tracer.go",
        "sdk/go/abi/transport_socket.go",
        "sdk/go/abi/udp_listener.go",
        "sdk/go/abi/upstream_http_tcp_bridge.go",
        "//source/extensions/dynamic_modules/abi:abi.h",
    ],
    cgo = True,
    clinkopts = select({
        "//bazel:darwin_any": [
            "-Wl,-undefined,dynamic_lookup",
        ],
        "//conditions:default": [],
    }),
    importpath = "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi",
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/dynamic_modules:go_sdk",
        "//source/extensions/dynamic_modules:go_sdk_shared",
    ],
)
