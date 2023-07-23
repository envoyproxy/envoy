load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_admin_functionality",
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
        "//:VERSION.txt",
        "//bazel:raw_build_id.ldscript",
        "//source/exe:envoy-static",
    ],
)

envoy_cc_test(
    name = "main_common_test",
    srcs = envoy_select_admin_functionality(["main_common_test.cc"]),
    data = [
        "//test/config/integration:google_com_proxy_port_0",
    ],
    deps = [
        "//source/common/api:api_lib",
        "//source/exe:envoy_main_common_with_core_extensions_lib",
        "//source/exe:platform_impl_lib",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:contention_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "extra_extensions_test",
    srcs = ["extra_extensions_test.cc"],
    deps = [
        "//test/test_common:environment_lib",
    ] + select({
        # gcc RBE build has trouble compiling target with all extensions
        "//bazel:gcc_build": ["//source/exe:envoy_main_common_with_core_extensions_lib"],
        # This dependency MUST be main_common_lib to meet the purpose of this test
        "//conditions:default": ["//source/exe:main_common_lib"],
    }),
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

envoy_cc_test(
    name = "check_extensions_against_registry_test",
    srcs = ["check_extensions_against_registry_test.cc"],
    data = [
        "//source/extensions:extensions_metadata.yaml",
    ],
    deps = [
        "//test/test_common:environment_lib",
    ] + select({
        # gcc RBE build has trouble compiling target with all extensions
        "//bazel:gcc_build": ["//source/exe:envoy_main_common_with_core_extensions_lib"],
        # This dependency MUST be main_common_lib to meet the purpose of this test
        "//conditions:default": ["//source/exe:main_common_lib"],
    }),
)

envoy_cc_test(
    name = "win32_scm_test",
    srcs = ["win32_scm_test.cc"],
    coverage = False,
    data = [
        "//source/exe:envoy-static",
        "//test/config/integration:google_com_proxy_port_0",
    ],
    deps = [
        "//source/common/api:api_lib",
        "//source/exe:envoy_main_common_with_core_extensions_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:contention_lib",
        "//test/test_common:environment_lib",
    ],
)

# Due to the limitiations of how Windows signals work
# this test cannot be executed through bazel and it must
# be executed as a standalone executable.
# e.g.: `./bazel-bin/test/exe/win32_outofproc_main_test.exe`
envoy_cc_test(
    name = "win32_outofproc_main_test",
    srcs = ["win32_outofproc_main_test.cc"],
    coverage = False,
    data = [
        "//source/exe:envoy-static",
        "//test/config/integration:google_com_proxy_port_0",
    ],
    # TODO(envoyproxy/windows-dev): Disable the manual tag.
    tags = ["manual"],
    deps = [
        "//source/common/api:api_lib",
        "//source/exe:main_common_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:contention_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "platform_impl_test",
    srcs = ["platform_impl_test.cc"],
    deps = [
        "//source/exe:platform_impl_lib",
        "//test/mocks/runtime:runtime_mocks",
    ],
)
