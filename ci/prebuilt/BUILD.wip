package(default_visibility = ["//visibility:public"])

cc_library(
    name = "ares",
    srcs = ["thirdparty_build/lib/libcares.a"],
    hdrs = glob(["thirdparty_build/include/ares*.h"]),
    strip_include_prefix = "thirdparty_build/include",
)

cc_library(
    name = "crypto",
    srcs = ["thirdparty_build/lib/libcrypto.a"],
    hdrs = glob(["thirdparty_build/include/openssl/**/*.h"]),
    strip_include_prefix = "thirdparty_build/include",
)

cc_library(
    name = "event",
    srcs = ["thirdparty_build/lib/libevent.a"],
    hdrs = glob(["thirdparty_build/include/event2/**/*.h"]),
    strip_include_prefix = "thirdparty_build/include",
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
    strip_include_prefix = "thirdparty_build/include",
)

cc_library(
    name = "spdlog",
    hdrs = glob([
        "thirdparty/spdlog-0.11.0/include/**/*.cc",
        "thirdparty/spdlog-0.11.0/include/**/*.h",
    ]),
    strip_include_prefix = "thirdparty/spdlog-0.11.0/include",
)

cc_library(
    name = "ssl",
    srcs = ["thirdparty_build/lib/libssl.a"],
    deps = [":crypto"],
)

cc_library(
    name = "tclap",
    hdrs = glob(["thirdparty/tclap-1.2.1/include/**/*.h"]),
    strip_include_prefix = "thirdparty/tclap-1.2.1/include",
)
