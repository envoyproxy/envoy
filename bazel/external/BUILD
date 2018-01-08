# Use a wrapper cc_library with an empty source source file to force
# compilation of other cc_library targets that only list *.a sources.
cc_library(
    name = "all_external",
    srcs = [":empty.cc"],
    deps = [
        "@io_opentracing_cpp//:opentracing",
        "@com_lightstep_tracer_cpp//:lightstep_tracer",
        "@com_google_googletest//:gtest",
    ],
)

genrule(
    name = "empty_cc",
    outs = ["empty.cc"],
    cmd = "touch \"$(@D)/empty.cc\"",
)
