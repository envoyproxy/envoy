REPOSITORY_LOCATIONS = dict(
    bazel_skylib = dict(
        sha256 = "b5f6abe419da897b7901f90cbab08af958b97a8f3575b0d3dd062ac7ce78541f",
        strip_prefix = "bazel-skylib-0.5.0",
        urls = ["https://github.com/bazelbuild/bazel-skylib/archive/0.5.0.tar.gz"],
    ),
    com_lyft_protoc_gen_validate = dict(
        sha256 = "d92c7f22929f495cf9f7d825c44f9190eda1d8256af321e3e8692570181b28a6",
        strip_prefix = "protoc-gen-validate-0.0.11",
        urls = ["https://github.com/lyft/protoc-gen-validate/archive/v0.0.11.tar.gz"],
    ),
    googleapis = dict(
        # May 23, 2018
        sha256 = "16f5b2e8bf1e747a32f9a62e211f8f33c94645492e9bbd72458061d9a9de1f63",
        strip_prefix = "googleapis-d642131a6e6582fc226caf9893cb7fe7885b3411",
        urls = ["https://github.com/googleapis/googleapis/archive/d642131a6e6582fc226caf9893cb7fe7885b3411.tar.gz"],
        # TODO(dio): Consider writing a Skylark macro for importing Google API proto.
    ),
    com_github_gogo_protobuf = dict(
        sha256 = "9f8c2ad49849ab063cd9fef67e77d49606640044227ecf7f3617ea2c92ef147c",
        strip_prefix = "protobuf-1.1.1",
        urls = ["https://github.com/gogo/protobuf/archive/v1.1.1.tar.gz"],
    ),
    prometheus_metrics_model = dict(
        # Nov 17, 2017
        sha256 = "783bdaf8ee0464b35ec0c8704871e1e72afa0005c3f3587f65d9d6694bf3911b",
        strip_prefix = "client_model-99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c",
        urls = ["https://github.com/prometheus/client_model/archive/99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c.tar.gz"],
    ),
    io_opencensus_trace = dict(
        # May 23, 2018
        sha256 = "1950f844d9f338ba731897a9bb526f9074c0487b3f274ce2ec3b4feaf0bef7e2",
        strip_prefix = "opencensus-proto-ab82e5fdec8267dc2a726544b10af97675970847/opencensus/proto/trace",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/ab82e5fdec8267dc2a726544b10af97675970847.tar.gz"],
    ),
)
