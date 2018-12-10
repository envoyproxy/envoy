load("@io_opencensus_cpp//opencensus:curl.bzl", "CURL_COPTS")

package(features = ["no_copts_tokenization"])

config_setting(
    name = "windows",
    values = {"cpu": "x64_windows"},
    visibility = ["//visibility:private"],
)

config_setting(
    name = "osx",
    values = {"cpu": "darwin"},
    visibility = ["//visibility:private"],
)

cc_library(
    name = "curl",
    srcs = glob([
        "lib/**/*.c",
    ]),
    hdrs = glob([
        "include/curl/*.h",
        "lib/**/*.h",
    ]),
    copts = CURL_COPTS + [
        "-DOS=\"os\"",
        "-DCURL_EXTERN_SYMBOL=__attribute__((__visibility__(\"default\")))",
    ],
    includes = [
        "include/",
        "lib/",
    ],
    visibility = ["//visibility:public"],
)
