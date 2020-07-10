load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_sh_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_sh_test(
    name = "build_id_test",
    srcs = ["build_id_test.sh"],
    coverage = False,
    data = [
        "//bazel:raw_build_id.ldscript",
        "//source/exe:envoy-static",
    ],
    # The Windows equivalent of a binaries' "link stamp" is a resource file descriptor of the
    # executable. Our build revision API and output of --version flags are sufficient for now.
    tags = ["skip_on_windows"],
)

envoy_sh_test(
    name = "envoy_static_test",
    srcs = ["envoy_static_test.sh"],
    coverage = False,
    data = ["//source/exe:envoy-static"],
    # TODO(Windows): expect to test to leverage dumpbin.exe to confirm we avoid msvcrt, see
    #   https://github.com/envoyproxy/envoy/pull/8280#pullrequestreview-290187328
    # Sanitizers doesn't like statically linked lib(std)c++ and libgcc, skip this test in that context.
    tags = [
        "no_san",
        "skip_on_windows",
    ],
)

envoy_sh_test(
    name = "pie_test",
    srcs = ["pie_test.sh"],
    coverage = False,
    data = ["//source/exe:envoy-static"],
    # Since VS2015 or even earlier, link.exe defaults to PIE generation
    tags = [
        "nofips",
        "skip_on_windows",
    ],
)

envoy_sh_test(
    name = "version_out_test",
    srcs = ["version_out_test.sh"],
    coverage = False,
    data = [
        "//:VERSION",
        "//bazel:raw_build_id.ldscript",
        "//source/exe:envoy-static",
    ],
)

envoy_cc_test(
    name = "main_common_test",
    srcs = ["main_common_test.cc"],
    data = ["//test/config/integration:google_com_proxy_port_0"],
    # Fails on windows with cr/lf yaml file checkouts
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/api:api_lib",
        "//source/exe:envoy_main_common_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:contention_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "terminate_handler_test",
    srcs = ["terminate_handler_test.cc"],
    tags = ["backtrace"],
    deps = [
        "//source/exe:terminate_handler_lib",
        "//test/test_common:utility_lib",
    ],
)
