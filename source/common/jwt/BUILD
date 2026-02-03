load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "jwt_lib",
    srcs = [
        "check_audience.cc",
        "jwks.cc",
        "jwt.cc",
        "status.cc",
        "struct_utils.cc",
        "verify.cc",
    ],
    hdrs = [
        "check_audience.h",
        "jwks.h",
        "jwt.h",
        "status.h",
        "struct_utils.h",
        "verify.h",
    ],
    deps = [
        "//bazel:boringssl",
        "//source/common/protobuf",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

envoy_cc_library(
    name = "simple_lru_cache_lib",
    hdrs = [
        "simple_lru_cache.h",
        "simple_lru_cache_inl.h",
    ],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)
