BAZEL_SKYLIB_RELEASE = "1.0.3"
BAZEL_SKYLIB_SHA256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c"

OPENCENSUS_PROTO_RELEASE = "0.3.0"
OPENCENSUS_PROTO_SHA256 = "b7e13f0b4259e80c3070b583c2f39e53153085a6918718b1c710caf7037572b0"

PGV_GIT_SHA = "278964a8052f96a2f514add0298098f63fb7f47f"  # June 9, 2020
PGV_SHA256 = "e368733c9fb7f8489591ffaf269170d7658cc0cd1ee322b601512b769446d3c8"

GOOGLEAPIS_GIT_SHA = "82944da21578a53b74e547774cf62ed31a05b841"  # Dec 2, 2019
GOOGLEAPIS_SHA = "a45019af4d3290f02eaeb1ce10990166978c807cb33a9692141a076ba46d1405"

PROMETHEUS_GIT_SHA = "60555c9708c786597e6b07bf846d0dc5c2a46f54"  # Jun 23, 2020
PROMETHEUS_SHA = "6748b42f6879ad4d045c71019d2512c94be3dd86f60965e9e31e44a3f464323e"

UDPA_RELEASE = "0.0.1"
UDPA_SHA256 = "83a7dcc316d741031f34c0409021432b74a39c4811845a177133f02f948fe2d8"

ZIPKINAPI_RELEASE = "0.2.2"
ZIPKINAPI_SHA256 = "688c4fe170821dd589f36ec45aaadc03a618a40283bc1f97da8fa11686fc816b"

RULES_PROTO_GIT_SHA = "40298556293ae502c66579620a7ce867d5f57311"  # Aug 17, 2020
RULES_PROTO_SHA256 = "aa1ee19226f707d44bee44c720915199c20c84a23318bb0597ed4e5c873ccbd5"

REPOSITORY_LOCATIONS = dict(
    bazel_skylib = dict(
        sha256 = BAZEL_SKYLIB_SHA256,
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/" + BAZEL_SKYLIB_RELEASE + "/bazel-skylib-" + BAZEL_SKYLIB_RELEASE + ".tar.gz"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        sha256 = PGV_SHA256,
        strip_prefix = "protoc-gen-validate-" + PGV_GIT_SHA,
        urls = ["https://github.com/envoyproxy/protoc-gen-validate/archive/" + PGV_GIT_SHA + ".tar.gz"],
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Starlark macro for importing Google API proto.
        sha256 = GOOGLEAPIS_SHA,
        strip_prefix = "googleapis-" + GOOGLEAPIS_GIT_SHA,
        urls = ["https://github.com/googleapis/googleapis/archive/" + GOOGLEAPIS_GIT_SHA + ".tar.gz"],
    ),
    com_github_cncf_udpa = dict(
        sha256 = UDPA_SHA256,
        strip_prefix = "udpa-" + UDPA_RELEASE,
        urls = ["https://github.com/cncf/udpa/archive/v" + UDPA_RELEASE + ".tar.gz"],
    ),
    prometheus_metrics_model = dict(
        sha256 = PROMETHEUS_SHA,
        strip_prefix = "client_model-" + PROMETHEUS_GIT_SHA,
        urls = ["https://github.com/prometheus/client_model/archive/" + PROMETHEUS_GIT_SHA + ".tar.gz"],
    ),
    opencensus_proto = dict(
        sha256 = OPENCENSUS_PROTO_SHA256,
        strip_prefix = "opencensus-proto-" + OPENCENSUS_PROTO_RELEASE + "/src",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/v" + OPENCENSUS_PROTO_RELEASE + ".tar.gz"],
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
