load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_test",
    "envoy_contrib_package",
)
load(
    "//contrib:all_contrib_extensions.bzl",
    "ARM64_SKIP_CONTRIB_TARGETS",
    "FIPS_LINUX_X86_SKIP_CONTRIB_TARGETS",
    "PPC_SKIP_CONTRIB_TARGETS",
    "envoy_all_contrib_extensions",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

alias(
    name = "envoy",
    actual = ":envoy-static",
)

SELECTED_CONTRIB_EXTENSIONS = select({
    "//bazel:linux_aarch64": envoy_all_contrib_extensions(ARM64_SKIP_CONTRIB_TARGETS),
    "//bazel:linux_ppc": envoy_all_contrib_extensions(PPC_SKIP_CONTRIB_TARGETS),
    "//bazel:boringssl_fips_x86": envoy_all_contrib_extensions(FIPS_LINUX_X86_SKIP_CONTRIB_TARGETS),
    "//conditions:default": envoy_all_contrib_extensions(),
})

envoy_cc_binary(
    name = "envoy-static",
    rbe_pool = "2core",
    stamped = True,
    visibility = ["//visibility:public"],
    deps = ["//source/exe:envoy_main_entry_lib"] + SELECTED_CONTRIB_EXTENSIONS,
)

envoy_cc_test(
    name = "example_configs_test",
    size = "large",
    data = [
        "//configs:example_contrib_configs",
        "//test/config_test:example_configs_test_setup.sh",
    ],
    env = {
        "EXAMPLE_CONFIGS_TAR_PATH": "envoy/configs/example_contrib_configs.tar",
        "DISABLE_TEST_MERGE": "true",
        "GODEBUG": "cgocheck=0",
    },
    rbe_pool = "2core",
    deps = [
        "//test/config_test:example_configs_test_lib",
    ] + SELECTED_CONTRIB_EXTENSIONS,
)
