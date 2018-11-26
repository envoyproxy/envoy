# Use a wrapper cc_library with an empty source source file to force
# compilation of other cc_library targets that only list *.a sources.
cc_library(
    name = "all_external",
    srcs = [":empty.cc"],
    deps = [
        "@com_google_googletest//:gtest",
        "@com_lightstep_tracer_cpp//:lightstep_tracer",
        "@com_github_datadog_dd_opentracing_cpp//:dd_opentracing_cpp",
        "@io_opentracing_cpp//:opentracing",
    ],
)

genrule(
    name = "empty_cc",
    outs = ["empty.cc"],
    cmd = "touch \"$(@D)/empty.cc\"",
)
