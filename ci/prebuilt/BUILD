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
    name = "luajit",
    srcs = ["thirdparty_build/lib/libluajit-5.1.a"],
    hdrs = glob(["thirdparty_build/include/luajit-2.0/*"]),
    includes = ["thirdparty_build/include"],
    # TODO(mattklein123): We should strip luajit-2.0 here for consumers. However, if we do that
    # the headers get included using -I vs. -isystem which then causes old-style-cast warnings.
)

cc_library(
    name = "nghttp2",
    srcs = ["thirdparty_build/lib/libnghttp2.a"],
    hdrs = glob(["thirdparty_build/include/nghttp2/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "rapidjson",
    hdrs = glob(["thirdparty/rapidjson/include/**/*.h"]),
    includes = ["thirdparty/rapidjson/include"],
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

cc_library(
    name = "xxhash",
    srcs = ["thirdparty_build/lib/xxhash.a"],
    hdrs = ["thirdparty_build/include/xxhash.h"],
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "yaml_cpp",
    srcs = ["thirdparty_build/lib/libyaml-cpp.a"],
    hdrs = glob(["thirdparty_build/include/yaml-cpp/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "zlib",
    srcs = ["thirdparty_build/lib/libz.a"],
    hdrs = [
        "thirdparty_build/include/zconf.h",
        "thirdparty_build/include/zlib.h",
    ],
)
