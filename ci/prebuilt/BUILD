licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "luajit",
    srcs = ["thirdparty_build/lib/libluajit-5.1.a"],
    hdrs = glob(["thirdparty_build/include/luajit-2.1/*"]),
    includes = ["thirdparty_build/include"],
    # TODO(mattklein123): We should strip luajit-2.1 here for consumers. However, if we do that
    # the headers get included using -I vs. -isystem which then causes old-style-cast warnings.
)

cc_library(
    name = "tcmalloc_and_profiler",
    srcs = ["thirdparty_build/lib/libtcmalloc_and_profiler.a"],
    hdrs = glob(["thirdparty_build/include/gperftools/**/*.h"]),
    strip_include_prefix = "thirdparty_build/include",
)

cc_library(
    name = "tcmalloc_debug",
    srcs = ["thirdparty_build/lib/libtcmalloc_debug.a"],
    hdrs = glob(["thirdparty_build/include/gperftools/**/*.h"]),
    strip_include_prefix = "thirdparty_build/include",
)
