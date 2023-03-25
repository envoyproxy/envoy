load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_package",
    "envoy_sh_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_binary(
    name = "test_server",
    srcs = ["test_server.cc"],
    external_deps = [
        "abseil_symbolize",
    ],
    stamped = True,
    deps = [
        "//source/exe:envoy_main_common_with_core_extensions_lib",
        "//source/exe:platform_impl_lib",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//source/extensions/clusters/static:static_cluster_lib",
        "//source/server/admin:admin_html_util",
    ],
)

# This is a complex test starting up a binary and fetching files, but it really
# only takes about 1.2 seconds.
envoy_sh_test(
    name = "test_server_test",
    size = "medium",
    srcs = ["test_server_test.sh"],
    cc_binary = [":test_server"],
    data = [
        ":test_server_files",
        "//test/integration:test_utility_files",
    ],
    # tags = ["skip_on_windows"],
)

filegroup(
    name = "test_server_files",
    srcs = [
        "active_stats_test.js",
        "web_test.html",
        "web_test.js",
        "web_test.yaml",
    ],
)
