load("@protobuf_git//:protobuf.bzl", "cc_proto_library")

exports_files(["source/precompiled/precompiled.h"])

package(default_visibility = ["//visibility:public"])

genrule(
    name = "envoy-ratelimit-proto",
    srcs = [
        "source/common/ratelimit/ratelimit.proto",
    ],
    outs = [
        "source/common/generated/ratelimit.proto",
    ],
    cmd = "cp $(SRCS) $@",
)

cc_proto_library(
    name = "envoy-ratelimit-pb",
    srcs = [
        "source/common/generated/ratelimit.proto",
    ],
    default_runtime = "//external:protobuf",
    protoc = "//external:protoc",
    include = "source",
)

genrule(
    name = "envoy-test-proto",
    srcs = [
        "test/proto/helloworld.proto",
    ],
    outs = [
        "test/generated/helloworld.proto",
    ],
    cmd = "cp $(SRCS) $@",
)

cc_proto_library(
    name = "envoy-test-pb",
    srcs = [
        "test/generated/helloworld.proto",
    ],
    default_runtime = "//external:protobuf",
    protoc = "//external:protoc",
    include = "test",
)

genrule(
    name = "envoy-version",
    srcs = glob([
        ".git/**",
    ]),
    tools = [
        "tools/gen_git_sha.sh",
    ],
    outs = [
        "source/common/version_generated.cc",
    ],
    cmd = "touch $@ && $(location tools/gen_git_sha.sh) $$(dirname $(location tools/gen_git_sha.sh)) $@",
    local = 1,
)

cc_library(
    name = "envoy-common",
    srcs = glob([
        "source/**/*.cc",
        "source/**/*.h",
        "include/**/*.h",
    ], exclude=["source/exe/main.cc"]) + [
        "source/common/version_generated.cc",
    ],
    copts = [
        "-includesource/precompiled/precompiled.h",
    ],
    includes = [
        "include",
        "source",
    ],
    linkopts = [
        "-lpthread",
        "-lanl",
        "-lrt",
    ],
    linkstatic=1,
    alwayslink=1,
    deps = [
        ":envoy-ratelimit-pb",
        "//external:libssl",
        "//external:nghttp2",
        "//external:spdlog",
        "//external:tclap",
        "//external:lightstep",
        "//external:event",
        "//external:protobuf",
        "//external:http_parser",
        "//external:rapidjson",
        "//external:event_pthreads",
    ],
)

cc_binary(
    name = "envoy",
    srcs = [
        "source/exe/main.cc",
    ],
    copts = [
        "-includesource/precompiled/precompiled.h",
    ],
    deps = [
        ":envoy-common",
    ],
    linkstatic=1,
)

cc_library(
    name = "envoy-test-lib",
    srcs = glob([
        "test/**/*.cc",
        "test/**/*.h",
    ]),
    copts = [
        "-includetest/precompiled/precompiled_test.h",
    ],
    deps = [
        ":envoy-common",
        ":envoy-test-pb",
        "//external:googletest",
    ],
    alwayslink=1,
)

filegroup(
    name = "envoy-testdata",
    srcs = glob([
        "generated/**/*",
        "test/**/*",
    ]),
)

cc_test(
    name = "envoy-test",
    data = [
        ":envoy-testdata",
    ],
    deps = [
        ":envoy-test-lib",
        ":envoy-test-pb",
        "//external:googletest",
    ],
    linkstatic=1,
)
