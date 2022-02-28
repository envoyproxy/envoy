load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr/custom_cel/extended_request/utility:utility_lib",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public/containers:container_backed_list_impl",
        "@com_google_cel_cpp//eval/public/containers:container_backed_map_impl",
    ],
)
