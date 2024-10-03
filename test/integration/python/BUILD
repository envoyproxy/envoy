load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_py_test")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_test(
    name = "hotrestart_handoff_test",
    # This is not actually large, but setting large makes the test skip for
    # asan and tsan.
    size = "large",
    srcs = select({
        "//bazel:disable_hot_restart_or_admin": ["null_test.py"],
        "//conditions:default": ["hotrestart_handoff_test.py"],
    }),
    args = [
        "--envoy-binary=$(location //test/integration:hotrestart_main)",
        "--h3-request=$(location //tools/h3_request)",
        "--ca-certs=$(location //test/config/integration/certs:cacert.pem)",
        "--ca-key=$(location //test/config/integration/certs:cakey.pem)",
    ],
    data = [
        "//test/config/integration/certs:cacert.pem",
        "//test/config/integration/certs:cakey.pem",
        "//test/integration:hotrestart_main",
        "//tools/h3_request",
    ],
    exec_properties = select({
        "//bazel:engflow_rbe": {"Pool": "2core"},
        "//conditions:default": {},
    }),
    main = select({
        "//bazel:disable_hot_restart_or_admin": "null_test.py",
        "//conditions:default": "hotrestart_handoff_test.py",
    }),
    # Hot restart does not apply on Windows.
    # py_test doesn't do coverage.
    tags = [
        "nocoverage",
        "skip_on_windows",
    ],
    deps = [
        requirement("aiohttp"),
        requirement("cryptography"),
    ],
)
