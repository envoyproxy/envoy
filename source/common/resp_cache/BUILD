licenses(["notice"])  # Apache 2

# exports_files(["resp_cache.h", "simple_cache.h"])

cc_library(
    name = "resp_cache",
    srcs = [
        # Add any .cc files if you have implementations in .cc files
    ],
    hdrs = [
        "resp_cache.h",
        "resp_cache_factory.h",
        "simple_cache.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        #"//source/common/common:thread",
        #"//source/common/common:time",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/container:flat_hash_map",
        "@envoy_api//envoy/extensions/filters/http/ext_authz/v3:pkg_cc_proto",
    ],
)
