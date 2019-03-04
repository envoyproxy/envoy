BAZEL_SKYLIB_RELEASE = "0.6.0"
BAZEL_SKYLIB_SHA256 = "eb5c57e4c12e68c0c20bc774bfbc60a568e800d025557bc4ea022c6479acc867"

GOGOPROTO_RELEASE = "1.2.0"
GOGOPROTO_SHA256 = "957c8f03cf595525d2a667035d9865a0930b3d446be0ab6eb76972934f925b00"

OPENCENSUS_RELEASE = "0.1.0"
OPENCENSUS_SHA256 = "4fd21cc6de63d7cb979fd749d8101ff425905aa0826fed26019d1c311fcf19a7"

PGV_RELEASE = "0.0.13"
PGV_SHA256 = "dce6c8a43849d2abe4d5e40f16e9a476bca6b7a47e128db4458a52d748f4a5eb"

GOOGLEAPIS_GIT_SHA = "d642131a6e6582fc226caf9893cb7fe7885b3411"  # May 23, 2018
GOOGLEAPIS_SHA = "16f5b2e8bf1e747a32f9a62e211f8f33c94645492e9bbd72458061d9a9de1f63"

PROMETHEUS_GIT_SHA = "99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c"  # Nov 17, 2017
PROMETHEUS_SHA = "783bdaf8ee0464b35ec0c8704871e1e72afa0005c3f3587f65d9d6694bf3911b"

REPOSITORY_LOCATIONS = dict(
    bazel_skylib = dict(
        sha256 = BAZEL_SKYLIB_SHA256,
        strip_prefix = "bazel-skylib-" + BAZEL_SKYLIB_RELEASE,
        urls = ["https://github.com/bazelbuild/bazel-skylib/archive/" + BAZEL_SKYLIB_RELEASE + ".tar.gz"],
    ),
    com_lyft_protoc_gen_validate = dict(
        sha256 = PGV_SHA256,
        strip_prefix = "protoc-gen-validate-" + PGV_RELEASE,
        urls = ["https://github.com/lyft/protoc-gen-validate/archive/v" + PGV_RELEASE + ".tar.gz"],
    ),
    googleapis = dict(
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
    io_opencensus_trace = dict(
        sha256 = OPENCENSUS_SHA256,
        strip_prefix = "opencensus-proto-" + OPENCENSUS_RELEASE + "/src/opencensus/proto/trace/v1",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/v" + OPENCENSUS_RELEASE + ".tar.gz"],
    ),
)
