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
    include = "source",
    default_runtime = "//external:protobuf",
    protoc = "//external:protoc",
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
    include = "test",
    default_runtime = "//external:protobuf",
    protoc = "//external:protoc",
)

genrule(
    name = "envoy-version",
    srcs = glob([
        ".git/**",
    ]),
    outs = [
        "source/common/version_generated.cc",
    ],
    cmd = "touch $@ && $(location tools/gen_git_sha.sh) $$(dirname $(location tools/gen_git_sha.sh)) $@",
    local = 1,
    tools = [
        "tools/gen_git_sha.sh",
    ],
)

cc_library(
    name = "envoy-common",
    srcs = glob(
        [
            "source/**/*.cc",
            "source/**/*.h",
            "include/**/*.h",
        ],
        exclude = ["source/exe/main.cc"],
    ) + [
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
    linkstatic = 1,
    deps = [
        ":envoy-ratelimit-pb",
        "//external:event",
        "//external:event_pthreads",
        "//external:http_parser",
        "//external:libssl",
        "//external:lightstep",
        "//external:nghttp2",
        "//external:protobuf",
        "//external:rapidjson",
        "//external:spdlog",
        "//external:tclap",
    ],
    alwayslink = 1,
)

cc_binary(
    name = "envoy",
    srcs = [
        "source/exe/main.cc",
    ],
    copts = [
        "-includesource/precompiled/precompiled.h",
    ],
    linkstatic = 1,
    deps = [
        ":envoy-common",
    ],
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
    alwayslink = 1,
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
    linkstatic = 1,
    deps = [
        ":envoy-test-lib",
        ":envoy-test-pb",
        "//external:googletest",
    ],
)
