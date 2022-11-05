licenses(["notice"])  # Apache 2

exports_files(["boringssl_fips.genrule_cmd"])

# Use a wrapper cc_library with an empty source source file to force
# compilation of other cc_library targets that only list *.a sources.
cc_library(
    name = "all_external",
    srcs = [":empty.cc"],
    defines = ["OPENTRACING_STATIC"],
    # TODO: external/io_opentracing_cpp/BUILD.bazel:19:1: Executing genrule
    # @io_opentracing_cpp//:generate_version_h failed - needs porting
    tags = ["skip_on_windows"],
    deps = [
        "@com_github_datadog_dd_opentracing_cpp//:dd_opentracing_cpp",
        "@com_google_googletest//:gtest",
        "@io_opentracing_cpp//:opentracing",
    ],
)

genrule(
    name = "empty_cc",
    outs = ["empty.cc"],
    cmd = "touch \"$(@D)/empty.cc\"",
    visibility = ["//visibility:public"],
)
