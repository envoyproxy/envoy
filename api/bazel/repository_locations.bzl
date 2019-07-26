BAZEL_SKYLIB_RELEASE = "0.8.0"
BAZEL_SKYLIB_SHA256 = "2ef429f5d7ce7111263289644d233707dba35e39696377ebab8b0bc701f7818e"

GOGOPROTO_RELEASE = "1.2.1"
GOGOPROTO_SHA256 = "99e423905ba8921e86817607a5294ffeedb66fdd4a85efce5eb2848f715fdb3a"

OPENCENSUS_PROTO_GIT_SHA = "d5d80953a8c2ff4633087af6933cd152678434bb"  # Mar 18, 2019
OPENCENSUS_PROTO_SHA256 = "a4e87a1da21d1b3a16674332c3ee6e2689d52f3532e2ff8cb4a626c8bcdabcfc"

PGV_GIT_SHA = "dd90514d96e35023ed20201f349542b0bc64461d"
PGV_SHA256 = "f7d67677fbec5b0f561f6ee6788223fb535d3864b53a28b6678e319f5d1f4f25"

GOOGLEAPIS_GIT_SHA = "be480e391cc88a75cf2a81960ef79c80d5012068"  # Jul 24, 2019
GOOGLEAPIS_SHA = "c1969e5b72eab6d9b6cfcff748e45ba57294aeea1d96fd04cd081995de0605c2"

PROMETHEUS_GIT_SHA = "99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c"  # Nov 17, 2017
PROMETHEUS_SHA = "783bdaf8ee0464b35ec0c8704871e1e72afa0005c3f3587f65d9d6694bf3911b"

KAFKA_SOURCE_SHA = "ae7a1696c0a0302b43c5b21e515c37e6ecd365941f68a510a7e442eebddf39a1"  # 2.2.0-rc2

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
    com_github_gogo_protobuf = dict(
        sha256 = GOGOPROTO_SHA256,
        strip_prefix = "protobuf-" + GOGOPROTO_RELEASE,
        urls = ["https://github.com/gogo/protobuf/archive/v" + GOGOPROTO_RELEASE + ".tar.gz"],
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
)
