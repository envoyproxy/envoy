visibility = ["//visibility:public"]

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "envoy_proxy_dynamic_modules_cpp_sdk",
    srcs = [
            "cpp_sdk_impl.cc",
            "cpp_sdk_abi_impl.cc"
           ],
    hdrs = [
        "cpp_sdk_impl.h",
        "cpp_sdk_inf.h",
    ],
    deps = [ 
        "//source/extensions/dynamic_modules:abi_version_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ]
)

cc_shared_library(
    name = "envoy_cpp_sdk",
    deps = [ 
            "//source/extensions/dynamic_modules:abi_version_lib",
            "//source/extensions/dynamic_modules:dynamic_modules_lib",
            ":envoy_proxy_dynamic_modules_cpp_sdk", 
           ],
)