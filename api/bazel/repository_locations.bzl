BAZEL_SKYLIB_RELEASE = "0.8.0"
BAZEL_SKYLIB_SHA256 = "2ef429f5d7ce7111263289644d233707dba35e39696377ebab8b0bc701f7818e"

OPENCENSUS_PROTO_GIT_SHA = "5cec5ea58c3efa81fa808f2bd38ce182da9ee731"  # Jul 25, 2019
OPENCENSUS_PROTO_SHA256 = "faeb93f293ff715b0cb530d273901c0e2e99277b9ed1c0a0326bca9ec5774ad2"

PGV_GIT_SHA = "fd7de029969b7c0ef8b754660b997399b6fd812a"  # Sep 4, 2019
PGV_SHA256 = "55c6ad4a1b405938524ab55b18349c824d3fc6eaef580e1ef2a9dfe39f737b9e"

GOOGLEAPIS_GIT_SHA = "be480e391cc88a75cf2a81960ef79c80d5012068"  # Jul 24, 2019
GOOGLEAPIS_SHA = "c1969e5b72eab6d9b6cfcff748e45ba57294aeea1d96fd04cd081995de0605c2"

PROMETHEUS_GIT_SHA = "99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c"  # Nov 17, 2017
PROMETHEUS_SHA = "783bdaf8ee0464b35ec0c8704871e1e72afa0005c3f3587f65d9d6694bf3911b"

KAFKA_SOURCE_SHA = "ae7a1696c0a0302b43c5b21e515c37e6ecd365941f68a510a7e442eebddf39a1"  # 2.2.0-rc2

UDPA_GIT_SHA = "4cbdcb9931ca743a915a7c5fda51b2ee793ed157"  # Aug 22, 2019
UDPA_SHA256 = "6291d0c0e3a4d5f08057ea7a00ed0b0ec3dd4e5a3b1cf20f803774680b5a806f"

ZIPKINAPI_RELEASE = "0.2.2"  # Aug 23, 2019
ZIPKINAPI_SHA256 = "688c4fe170821dd589f36ec45aaadc03a618a40283bc1f97da8fa11686fc816b"

REPOSITORY_LOCATIONS = dict(
    bazel_skylib = dict(
        sha256 = BAZEL_SKYLIB_SHA256,
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/" + BAZEL_SKYLIB_RELEASE + "/bazel-skylib." + BAZEL_SKYLIB_RELEASE + ".tar.gz"],
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
    kafka_source = dict(
        sha256 = KAFKA_SOURCE_SHA,
        strip_prefix = "kafka-2.2.0-rc2/clients/src/main/resources/common/message",
        urls = ["https://github.com/apache/kafka/archive/2.2.0-rc2.zip"],
    ),
    com_github_openzipkin_zipkinapi = dict(
        sha256 = ZIPKINAPI_SHA256,
        strip_prefix = "zipkin-api-" + ZIPKINAPI_RELEASE,
        urls = ["https://github.com/openzipkin/zipkin-api/archive/" + ZIPKINAPI_RELEASE + ".tar.gz"],
    ),
)
