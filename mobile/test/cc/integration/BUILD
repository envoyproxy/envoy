load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "send_headers_test",
    srcs = ["send_headers_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:envoy_engine_cc_lib_no_stamp",
    ],
)

envoy_cc_test(
    name = "lifetimes_test",
    srcs = ["lifetimes_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:envoy_engine_cc_lib_no_stamp",
    ],
)
