load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_package",
    "envoy_select_admin_html",
    "envoy_sh_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_binary(
    name = "test_server",
    srcs = ["test_server.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/exe:envoy_main_common_with_core_extensions_lib",
        "//source/exe:platform_impl_lib",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//source/extensions/clusters/static:static_cluster_lib",
        "//source/server/admin:admin_html_util",
        "@com_google_absl//absl/debugging:symbolize",
    ],
)

# This is a complex test starting up a binary and fetching files, but it really
# only takes a couple of seconds.
envoy_sh_test(
    name = "test_server_test",
    size = "large",
    srcs = envoy_select_admin_html(["test_server_test.sh"]),
    cc_binary = [":test_server"],
    data = [
        ":test_server_files",
        "//source/server/admin/html:admin_web_files",
        "//test/integration:test_utility.sh",
    ],
    rbe_pool = "2core",

    # This test fails on windows trying to poll the server for the admin
    # address. Failure is:
    #     wait_for_admin_returning_admin_address C:/botcode/w39/_tmp/0988ea9ff3edc2e43abb999546a16771/test/integration/admin_html/tempfiles/admin.address
    #     cat: 'C:/botcode/w39/_tmp/0988ea9ff3edc2e43abb999546a16771/test/integration/admin_html/tempfiles/admin.address': No such file or directory
    #    curl: (3) URL using bad/illegal format or missing URL
    #
    # This seems fixable, if someone is so inclined.
    tags = [
        "nocoverage",
        "skip_on_windows",
    ],
)

filegroup(
    name = "test_server_files",
    srcs = [
        "active_stats_test.js",
        "histograms_test.js",
        "web_test.html",
        "web_test.js",
        "web_test.yaml",
    ],
)
