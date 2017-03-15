package(default_visibility = ["//visibility:public"])

cc_library(
    name = "googletest_main",
    hdrs = glob([
        "thirdparty_build/include/gmock/**/*.h",
        "thirdparty_build/include/gtest/**/*.h",
    ]),
    srcs = [
        "thirdparty_build/lib/libgmock.a",
        "thirdparty_build/lib/libgtest.a",
        "thirdparty_build/lib/libgtest_main.a",
    ],
    strip_include_prefix = "thirdparty_build/include",
)

cc_library(
    name = "spdlog",
    hdrs = glob([
        "thirdparty/spdlog-0.11.0/include/**/*.h",
        "thirdparty/spdlog-0.11.0/include/**/*.cc",
    ]),
    strip_include_prefix = "thirdparty/spdlog-0.11.0/include",
)
