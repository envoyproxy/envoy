load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")

licenses(["notice"])  # Apache 2

package(
    default_visibility = ["//visibility:public"],
)

swift_library(
    name = "FlatBuffers",
    srcs = glob(["swift/Sources/FlatBuffers/*.swift"]),
    module_name = "FlatBuffers",
)
