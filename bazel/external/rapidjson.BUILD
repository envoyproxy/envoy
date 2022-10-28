licenses(["notice"])  # Apache 2

cc_library(
    name = "rapidjson",
    hdrs = glob(["include/rapidjson/**/*.h"]),
    defines = ["RAPIDJSON_HAS_STDSTRING=1"],
    includes = ["include"],
    # rapidjson is only needed to build external dependency of the Zipkin tracer.
    # For Envoy source code plese use source/common/json/json_loader.h
    visibility = ["@io_opencensus_cpp//opencensus/exporters/trace/zipkin:__pkg__"],
)
