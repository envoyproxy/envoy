package(default_visibility = ["//visibility:public"])

cc_library(
    name = "antlr4",
    srcs = glob([
        "runtime/Cpp/runtime/src/**/*.cpp",
    ]),
    hdrs = glob([
        "runtime/Cpp/runtime/src/**/*.h",
    ]),
    includes = [
        "runtime/Cpp/runtime/src",
    ],
)

# Alias for compatibility with cel-cpp
cc_library(
    name = "antlr4-cpp-runtime",
    deps = [":antlr4"],
)
