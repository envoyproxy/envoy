licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_import(
    name = "linklib",
    shared_library = "library/linux-classic_interp-multi_module-dbg/libiwasm.so",
)

cc_library(
    name = "headlib",
    hdrs = glob(["include/*.h"]),
    srcs = glob(["include/*.c"]),
    include_prefix = "wamr",
)

cc_library(
    name = "wamr_lib",
    defines = ["WASM_WAMR"],
    deps = [
        "linklib",
        "headlib",
    ],
)
