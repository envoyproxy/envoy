licenses(["notice"])  # Apache 2

exports_files([
    "EnvoyEngine.h",
])

objc_library(
    name = "envoy_engine_objc_lib",
    # TODO(@goaway): The headers here should really be in hdrs, but this causes them to be imported
    # twice due to the way they're pulled into our swift framework rule. We should fix this to
    # maintain a valid objc target.
    srcs = [
        "EnvoyEngine.m",
    ],
    hdrs = [
        "EnvoyEngine.h",
    ],
    visibility = ["//visibility:public"],
    deps = ["//library/common:envoy_main_interface_lib"],
)
