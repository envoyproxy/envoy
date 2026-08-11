load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Compiles the sock_ops and sk_msg programs into the eBPF object loaded at runtime. The -g flag is
# required so clang emits the BTF that the .maps definition is parsed from. Tagged manual because it
# shells out to clang with the bpf target and is only needed when sockmap is enabled.
genrule(
    name = "sockmap_bpf",
    srcs = ["bpf/sockmap_kern.c"],
    outs = ["sockmap_kern.o"],
    cmd = "clang -O2 -g -target bpf -I/usr/include/$$(uname -m)-linux-gnu -c $(location bpf/sockmap_kern.c) -o $@",
    tags = [
        "manual",
        "no-remote-exec",
        "skip_on_windows",
    ],
    target_compatible_with = ["@platforms//os:linux"],
)

envoy_cc_library(
    name = "bpf_datapath_lib",
    srcs = ["bpf_datapath.cc"],
    hdrs = ["bpf_datapath.h"],
    defines = select({
        "//bazel:sockmap_enabled": ["ENVOY_ENABLE_SOCKMAP=1"],
        "//conditions:default": [],
    }),
    target_compatible_with = ["@platforms//os:linux"],
    deps = [
        "//envoy/network:address_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
    ] + select({
        "//bazel:sockmap_enabled": ["//bazel/foreign_cc:libbpf_linux"],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "io_handle_lib",
    srcs = ["io_handle.cc"],
    hdrs = ["io_handle.h"],
    target_compatible_with = ["@platforms//os:linux"],
    deps = [
        ":bpf_datapath_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/network:default_socket_interface_lib",
    ],
)

# Keep the extension target itself compatible everywhere so the static entry in
# extensions_build_config.bzl remains valid for tooling, while making it a no-op off Linux.
envoy_cc_extension(
    name = "config",
    srcs = select({
        "//bazel:linux": ["socket_interface.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:linux": ["socket_interface.h"],
        "//conditions:default": [],
    }),
    deps = select({
        "//bazel:linux": [
            ":bpf_datapath_lib",
            ":io_handle_lib",
            "//envoy/common:exception_lib",
            "//envoy/registry",
            "//envoy/server:bootstrap_extension_config_interface",
            "//source/common/common:fmt_lib",
            "//source/common/network:default_socket_interface_lib",
            "//source/common/network:socket_interface_lib",
            "//source/common/protobuf:utility_lib",
            "@envoy_api//envoy/extensions/network/socket_interface/sockmap/v3:pkg_cc_proto",
        ],
        "//conditions:default": [],
    }),
)
