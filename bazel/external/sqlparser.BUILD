licenses(["notice"])  # Apache 2

cc_library(
    name = "sqlparser",
    srcs = glob(["src/**/*.cpp"]),
    hdrs = glob([
        "include/**/*.h",
        "src/**/*.h",
    ]),
    defines = select({
        "@envoy//bazel:windows_x86_64": ["YY_NO_UNISTD_H"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
