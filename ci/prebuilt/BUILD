licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ares",
    srcs = ["thirdparty_build/lib/libcares.a"],
    hdrs = glob(["thirdparty_build/include/ares*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "backward",
    hdrs = ["thirdparty_build/include/backward.hpp"],
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "crypto",
    srcs = ["thirdparty_build/lib/libcrypto.a"],
    hdrs = glob(["thirdparty_build/include/openssl/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "event",
    srcs = ["thirdparty_build/lib/libevent.a"],
    hdrs = glob(["thirdparty_build/include/event2/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "event_pthreads",
    srcs = ["thirdparty_build/lib/libevent_pthreads.a"],
    deps = [":event"],
)

cc_library(
    name = "googletest",
    srcs = [
        "thirdparty_build/lib/libgmock.a",
        "thirdparty_build/lib/libgtest.a",
    ],
    hdrs = glob([
        "thirdparty_build/include/gmock/**/*.h",
        "thirdparty_build/include/gtest/**/*.h",
    ]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "http_parser",
    srcs = ["thirdparty_build/lib/libhttp_parser.a"],
    hdrs = glob(["thirdparty_build/include/http_parser.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "lightstep",
    srcs = ["thirdparty_build/lib/liblightstep_core_cxx11.a"],
    hdrs = glob([
        "thirdparty_build/include/lightstep/**/*.h",
        "thirdparty_build/include/mapbox_variant/**/*.hpp",
    ]) + [
        "thirdparty_build/include/collector.pb.h",
        "thirdparty_build/include/lightstep_carrier.pb.h",
    ],
    includes = ["thirdparty_build/include"],
    deps = [":protobuf"],
)

cc_library(
    name = "nghttp2",
    srcs = ["thirdparty_build/lib/libnghttp2.a"],
    hdrs = glob(["thirdparty_build/include/nghttp2/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "protobuf",
    srcs = glob(["thirdparty_build/lib/libproto*.a"]),
    hdrs = glob(["thirdparty_build/include/google/protobuf/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

filegroup(
    name = "protoc",
    srcs = ["thirdparty_build/bin/protoc"],
)

cc_library(
    name = "rapidjson",
    hdrs = glob(["thirdparty/rapidjson/include/**/*.h"]),
    includes = ["thirdparty/rapidjson/include"],
)

cc_library(
    name = "spdlog",
    hdrs = glob([
        "thirdparty/spdlog/include/**/*.cc",
        "thirdparty/spdlog/include/**/*.h",
    ]),
    includes = ["thirdparty/spdlog/include"],
)

cc_library(
    name = "ssl",
    srcs = ["thirdparty_build/lib/libssl.a"],
    deps = [":crypto"],
)

cc_library(
    name = "tclap",
    hdrs = glob(["thirdparty/tclap/include/**/*.h"]),
    includes = ["thirdparty/tclap/include"],
)

cc_library(
    name = "tcmalloc_and_profiler",
    srcs = ["thirdparty_build/lib/libtcmalloc_and_profiler.a"],
    hdrs = glob(["thirdparty_build/include/gperftools/**/*.h"]),
    strip_include_prefix = "thirdparty_build/include",
)
