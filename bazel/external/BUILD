load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])  # Apache 2

exports_files([
    "aws_lc.genrule_cmd",
    "boringssl_fips.genrule_cmd",
])

# Use a wrapper cc_library with an empty source source file to force
# compilation of other cc_library targets that only list *.a sources.
cc_library(
    name = "all_external",
    srcs = [":empty.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "@dd-trace-cpp//:dd_trace_cpp",
        "@googletest//:gtest",
    ],
)

genrule(
    name = "empty_cc",
    outs = ["empty.cc"],
    cmd = "touch \"$(@D)/empty.cc\"",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "gperftools",
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:debug_tcmalloc": [
            "@gperftools//:stacktrace",
            "@gperftools//:tcmalloc_debug",
        ],
        "//conditions:default": [
            "@gperftools//:cpu_profiler",
            "@gperftools//:stacktrace",
            "@gperftools//:tcmalloc",
        ],
    }),
)

# Create a proper static library archive from the cc_library.
cc_static_library(
    name = "numa_static",
    target_compatible_with = ["@platforms//os:linux"],
    deps = ["@numactl//:numa"],
)

# Create a properly named libnuma.a archive for foreign_cc dependencies.
# The cc_library above produces Bazel-internal archives, but foreign_cc
# configure_make rules (like qatlib) expect a traditional libnuma.a file
# that can be found with -lnuma.
genrule(
    name = "numa_archive",
    srcs = [":numa_static"],
    outs = ["lib/libnuma.a"],
    cmd = "mkdir -p $$(dirname $@) && cp $< $@",
    target_compatible_with = ["@platforms//os:linux"],
    visibility = ["//visibility:public"],
)

# Create a proper static library archive from zlib-ng cc_library.
cc_static_library(
    name = "zlib_static",
    target_compatible_with = ["@platforms//os:linux"],
    deps = ["//bazel:zlib"],
)

# Create a properly named libz.a archive for foreign_cc dependencies.
# The zlib-ng cc_library produces Bazel-internal archives, but foreign_cc
# configure_make rules (like qatzip) expect a traditional libz.a file
# that can be found with -lz.
genrule(
    name = "zlib_archive",
    srcs = [":zlib_static"],
    outs = ["lib/libz.a"],
    cmd = "mkdir -p $$(dirname $@) && cp $< $@",
    target_compatible_with = ["@platforms//os:linux"],
    visibility = ["//visibility:public"],
)
