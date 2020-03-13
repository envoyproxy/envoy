BAZEL_SKYLIB_RELEASE = "0.9.0"
BAZEL_SKYLIB_SHA256 = "1dde365491125a3db70731e25658dfdd3bc5dbdfd11b840b3e987ecf043c7ca0"

OPENCENSUS_PROTO_GIT_SHA = "be218fb6bd674af7519b1850cdf8410d8cbd48e8"  # Dec 20, 2019
OPENCENSUS_PROTO_SHA256 = "e3bbdc94375e86c0edfb2fc5851507e08a3f26ee725ffff7c5c0e73264bdfcde"

PGV_GIT_SHA = "ab56c3dd1cf9b516b62c5087e1ec1471bd63631e"  # Mar 11, 2020
PGV_SHA256 = "3be12077affd1ebf8787001f5fba545cc5f1b914964dab4e0cc77c43fba03b41"

GOOGLEAPIS_GIT_SHA = "82944da21578a53b74e547774cf62ed31a05b841"  # Dec 2, 2019
GOOGLEAPIS_SHA = "a45019af4d3290f02eaeb1ce10990166978c807cb33a9692141a076ba46d1405"

PROMETHEUS_GIT_SHA = "99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c"  # Nov 17, 2017
PROMETHEUS_SHA = "783bdaf8ee0464b35ec0c8704871e1e72afa0005c3f3587f65d9d6694bf3911b"

UDPA_GIT_SHA = "db4b343e48c1264bb4d9ff491b059300701dc7c7"  # Jan 24, 2020
UDPA_SHA256 = "800624f44592a24898f133e39ae7fbb7a6c4b85bdddd448185fb7e277f097a56"

ZIPKINAPI_RELEASE = "0.2.2"  # Aug 23, 2019
ZIPKINAPI_SHA256 = "688c4fe170821dd589f36ec45aaadc03a618a40283bc1f97da8fa11686fc816b"

RULES_PROTO_GIT_SHA = "2c0468366367d7ed97a1f702f9cd7155ab3f73c5"  # Nov 19, 2019
RULES_PROTO_SHA256 = "73ebe9d15ba42401c785f9d0aeebccd73bd80bf6b8ac78f74996d31f2c0ad7a6"

REPOSITORY_LOCATIONS = dict(
    bazel_skylib = dict(
        sha256 = BAZEL_SKYLIB_SHA256,
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/" + BAZEL_SKYLIB_RELEASE + "/bazel_skylib-" + BAZEL_SKYLIB_RELEASE + ".tar.gz"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        sha256 = PGV_SHA256,
        strip_prefix = "protoc-gen-validate-" + PGV_GIT_SHA,
        urls = ["https://github.com/envoyproxy/protoc-gen-validate/archive/" + PGV_GIT_SHA + ".tar.gz"],
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Skylark macro for importing Google API proto.
        sha256 = GOOGLEAPIS_SHA,
        strip_prefix = "googleapis-" + GOOGLEAPIS_GIT_SHA,
        urls = ["https://github.com/googleapis/googleapis/archive/" + GOOGLEAPIS_GIT_SHA + ".tar.gz"],
    ),
    com_github_cncf_udpa = dict(
        sha256 = UDPA_SHA256,
        strip_prefix = "udpa-" + UDPA_GIT_SHA,
        urls = ["https://github.com/cncf/udpa/archive/" + UDPA_GIT_SHA + ".tar.gz"],
    ),
    prometheus_metrics_model = dict(
        sha256 = PROMETHEUS_SHA,
        strip_prefix = "client_model-" + PROMETHEUS_GIT_SHA,
        urls = ["https://github.com/prometheus/client_model/archive/" + PROMETHEUS_GIT_SHA + ".tar.gz"],
    ),
    opencensus_proto = dict(
        sha256 = OPENCENSUS_PROTO_SHA256,
        strip_prefix = "opencensus-proto-" + OPENCENSUS_PROTO_GIT_SHA + "/src",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/" + OPENCENSUS_PROTO_GIT_SHA + ".tar.gz"],
    ),
    rules_proto = dict(
        sha256 = RULES_PROTO_SHA256,
        strip_prefix = "rules_proto-" + RULES_PROTO_GIT_SHA + "",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/" + RULES_PROTO_GIT_SHA + ".tar.gz"],
    ),
    com_github_openzipkin_zipkinapi = dict(
        sha256 = ZIPKINAPI_SHA256,
        strip_prefix = "zipkin-api-" + ZIPKINAPI_RELEASE,
        urls = ["https://github.com/openzipkin/zipkin-api/archive/" + ZIPKINAPI_RELEASE + ".tar.gz"],
    ),
)
