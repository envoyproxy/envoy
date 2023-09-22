load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)
load("//bazel:repositories.bzl", "PPC_SKIP_TARGETS", "WINDOWS_SKIP_TARGETS")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_binary(
    name = "schema_validator_tool",
    srcs = ["schema_validator.cc"],
    deps = [":schema_validator_lib"] + select({
        "//bazel:windows_x86_64": envoy_all_extensions(WINDOWS_SKIP_TARGETS),
        "//bazel:linux_ppc": envoy_all_extensions(PPC_SKIP_TARGETS),
        "//conditions:default": envoy_all_extensions(),
    }),
)

envoy_cc_test_library(
    name = "schema_validator_lib",
    srcs = ["validator.cc"],
    hdrs = ["validator.h"],
    # TCLAP command line parser needs this to support int64_t/uint64_t in several build environments.
    copts = ["-DHAVE_LONG_LONG"],
    external_deps = ["tclap"],
    deps = [
        "//envoy/api:api_interface",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/version:version_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
