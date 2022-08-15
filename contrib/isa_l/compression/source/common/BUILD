load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)
load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

configure_make(
    name = "libnasm",
    autogen = True,
    configure_in_place = True,
    configure_options = ["CFLAGS='-Dredacted=\"redacted\"'"],
    lib_source = "@com_github_netwide_assembler_nasm//:all",
    out_binaries = ["nasm"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
)

configure_make(
    name = "isa_l",
    autogen = True,
    configure_in_place = True,
    env = {"PATH": "$$PATH:$$EXT_BUILD_DEPS/libnasm/bin"},
    lib_source = "@com_github_intel_isa_l//:all",
    out_static_libs = ["libisal.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    deps = [":libnasm"],
)

envoy_cc_library(
    name = "igzip_base_lib",
    srcs = ["base.cc"],
    hdrs = ["base.h"],
    deps = [
        ":isa_l",
        "//source/common/buffer:buffer_lib",
    ],
)
