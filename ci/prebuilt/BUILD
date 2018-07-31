licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

config_setting(
    name = "windows_x86_64",
    values = {"cpu": "x64_windows"},
)

cc_library(
    name = "ares",
    srcs = select({
        ":windows_x86_64": ["thirdparty_build/lib/cares.lib"],
        "//conditions:default": ["thirdparty_build/lib/libcares.a"],
    }),
    hdrs = glob(["thirdparty_build/include/ares*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "benchmark",
    srcs = select({
        ":windows_x86_64": ["thirdparty_build/lib/benchmark.lib"],
        "//conditions:default": ["thirdparty_build/lib/libbenchmark.a"],
    }),
    hdrs = ["thirdparty_build/include/testing/base/public/benchmark.h"],
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "event",
    srcs = select({
        ":windows_x86_64": ["thirdparty_build/lib/event.lib"],
        "//conditions:default": ["thirdparty_build/lib/libevent.a"],
    }),
    hdrs = glob(["thirdparty_build/include/event2/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "luajit",
    srcs = select({
        ":windows_x86_64": ["thirdparty_build/lib/luajit.lib"],
        "//conditions:default": ["thirdparty_build/lib/libluajit-5.1.a"],
    }),
    hdrs = glob(["thirdparty_build/include/luajit-2.0/*"]),
    includes = ["thirdparty_build/include"],
    # TODO(mattklein123): We should strip luajit-2.0 here for consumers. However, if we do that
    # the headers get included using -I vs. -isystem which then causes old-style-cast warnings.
)

cc_library(
    name = "nghttp2",
    srcs = select({
        ":windows_x86_64": ["thirdparty_build/lib/nghttp2.lib"],
        "//conditions:default": ["thirdparty_build/lib/libnghttp2.a"],
    }),
    hdrs = glob(["thirdparty_build/include/nghttp2/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "tcmalloc_and_profiler",
    srcs = ["thirdparty_build/lib/libtcmalloc_and_profiler.a"],
    hdrs = glob(["thirdparty_build/include/gperftools/**/*.h"]),
    strip_include_prefix = "thirdparty_build/include",
)

cc_library(
    name = "yaml_cpp",
    srcs = select({
        ":windows_x86_64": glob(["thirdparty_build/lib/libyaml-cpp*.lib"]),
        "//conditions:default": ["thirdparty_build/lib/libyaml-cpp.a"],
    }),
    hdrs = glob(["thirdparty_build/include/yaml-cpp/**/*.h"]),
    includes = ["thirdparty_build/include"],
)

cc_library(
    name = "zlib",
    srcs = select({
        ":windows_x86_64": glob(["thirdparty_build/lib/zlibstaticd.lib"]),
        "//conditions:default": ["thirdparty_build/lib/libz.a"],
    }),
    hdrs = [
        "thirdparty_build/include/zconf.h",
        "thirdparty_build/include/zlib.h",
    ],
    includes = ["thirdparty_build/include"],
)
