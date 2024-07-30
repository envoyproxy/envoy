# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_skylib = dict(
        project_name = "bazel-skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.7.1",
        sha256 = "bc283cdfcd526a52c3201279cda4bc298652efa898b10b4db0837dc51652756f",
        release_date = "2024-06-03",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/bazelbuild/bazel-skylib/blob/{version}/LICENSE",
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        project_name = "protoc-gen-validate (PGV)",
        project_desc = "protoc plugin to generate polyglot message validators",
        project_url = "https://github.com/bufbuild/protoc-gen-validate",
        use_category = ["api"],
        sha256 = "9372f9ecde8fbadf83c8c7de3dbb49b11067aa26fb608c501106d0b4bf06c28f",
        version = "1.0.4",
        urls = ["https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v{version}.zip"],
        strip_prefix = "protoc-gen-validate-{version}",
        release_date = "2024-01-17",
        implied_untracked_deps = [
            "com_github_iancoleman_strcase",
            "com_github_lyft_protoc_gen_star_v2",
            "com_github_spf13_afero",
            "org_golang_google_genproto",
            "org_golang_x_text",
            "org_golang_x_mod",
            "org_golang_x_sys",
        ],
        license = "Apache-2.0",
        license_url = "https://github.com/bufbuild/protoc-gen-validate/blob/v{version}/LICENSE",
    ),
    rules_jvm_external = dict(
        project_name = "Java Rules for Bazel",
        project_desc = "Bazel rules for Java",
        project_url = "https://github.com/bazelbuild/rules_jvm_external",
        version = "6.2",
        strip_prefix = "rules_jvm_external-{version}",
        sha256 = "808cb5c30b5f70d12a2a745a29edc46728fd35fa195c1762a596b63ae9cebe05",
        urls = ["https://github.com/bazelbuild/rules_jvm_external/releases/download/{version}/rules_jvm_external-{version}.tar.gz"],
        release_date = "2024-07-04",
        use_category = ["build"],
        license = "Apache-2.0",
        license_url = "https://github.com/bazelbuild/rules_jvm_external/blob/{version}/LICENSE",
    ),
    com_github_cncf_xds = dict(
        project_name = "xDS API",
        project_desc = "xDS API Working Group (xDS-WG)",
        project_url = "https://github.com/cncf/xds",
        # During the UDPA -> xDS migration, we aren't working with releases.
        version = "555b57ec207be86f811fb0c04752db6f85e3d7e2",
        sha256 = "0c8c4f0f67fed967b51049f7d5e2ca7a9bd433970a29c88e272c8665328172f5",
        release_date = "2024-04-23",
        strip_prefix = "xds-{version}",
        urls = ["https://github.com/cncf/xds/archive/{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/cncf/xds/blob/{version}/LICENSE",
    ),
    com_github_openzipkin_zipkinapi = dict(
        project_name = "Zipkin API",
        project_desc = "Zipkin's language independent model and HTTP Api Definitions",
        project_url = "https://github.com/openzipkin/zipkin-api",
        version = "1.0.0",
        sha256 = "6c8ee2014cf0746ba452e5f2c01f038df60e85eb2d910b226f9aa27ddc0e44cf",
        release_date = "2020-11-22",
        strip_prefix = "zipkin-api-{version}",
        urls = ["https://github.com/openzipkin/zipkin-api/archive/{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/openzipkin/zipkin-api/blob/{version}/LICENSE",
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Starlark macro for importing Google API proto.
        project_name = "Google APIs",
        project_desc = "Public interface definitions of Google APIs",
        project_url = "https://github.com/googleapis/googleapis",
        version = "adcf14f548fe22465404cec07c121fa26f1c5065",
        sha256 = "57019d06103b6211aaac79a8cf813a78cf214e8002b77137e402df31a1fe1aee",
        release_date = "2024-07-30",
        strip_prefix = "googleapis-{version}",
        urls = ["https://github.com/googleapis/googleapis/archive/{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/googleapis/googleapis/blob/{version}/LICENSE",
    ),
    opencensus_proto = dict(
        project_name = "OpenCensus Proto",
        project_desc = "Language Independent Interface Types For OpenCensus",
        project_url = "https://github.com/census-instrumentation/opencensus-proto",
        version = "0.4.1",
        sha256 = "e3d89f7f9ed84c9b6eee818c2e9306950519402bf803698b15c310b77ca2f0f3",
        release_date = "2022-09-23",
        strip_prefix = "opencensus-proto-{version}/src",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/v{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/census-instrumentation/opencensus-proto/blob/v{version}/LICENSE",
    ),
    prometheus_metrics_model = dict(
        project_name = "Prometheus client model",
        project_desc = "Data model artifacts for Prometheus",
        project_url = "https://github.com/prometheus/client_model",
        version = "0.6.1",
        sha256 = "b9b690bc35d80061f255faa7df7621eae39fe157179ccd78ff6409c3b004f05e",
        release_date = "2024-04-03",
        strip_prefix = "client_model-{version}",
        urls = ["https://github.com/prometheus/client_model/archive/v{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/prometheus/client_model/blob/v{version}/LICENSE",
    ),
    rules_proto = dict(
        project_name = "Protobuf Rules for Bazel",
        project_desc = "Protocol buffer rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_proto",
        version = "6.0.2",
        sha256 = "6fb6767d1bef535310547e03247f7518b03487740c11b6c6adb7952033fe1295",
        release_date = "2024-06-17",
        strip_prefix = "rules_proto-{version}",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/refs/tags/{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/bazelbuild/rules_proto/blob/{version}/LICENSE",
    ),
    opentelemetry_proto = dict(
        project_name = "OpenTelemetry Proto",
        project_desc = "Language Independent Interface Types For OpenTelemetry",
        project_url = "https://github.com/open-telemetry/opentelemetry-proto",
        version = "1.3.2",
        sha256 = "c069c0d96137cf005d34411fa67dd3b6f1f8c64af1e7fb2fe0089a41c425acd7",
        release_date = "2024-06-28",
        strip_prefix = "opentelemetry-proto-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-proto/archive/v{version}.tar.gz"],
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/open-telemetry/opentelemetry-proto/blob/v{version}/LICENSE",
    ),
    com_github_bufbuild_buf = dict(
        project_name = "buf",
        project_desc = "A new way of working with Protocol Buffers.",  # Used for breaking change detection in API protobufs
        project_url = "https://buf.build",
        version = "1.35.0",
        sha256 = "a75c622b5d6fae792a0e64a04baa296681eacba7ce0c3c35d25c8b42da2f71e1",
        strip_prefix = "buf",
        urls = ["https://github.com/bufbuild/buf/releases/download/v{version}/buf-Linux-x86_64.tar.gz"],
        release_date = "2024-07-22",
        use_category = ["api"],
        license = "Apache-2.0",
        license_url = "https://github.com/bufbuild/buf/blob/v{version}/LICENSE",
    ),
    com_github_chrusty_protoc_gen_jsonschema = dict(
        project_name = "protoc-gen-jsonschema",
        project_desc = "Protobuf to JSON-Schema compiler",
        project_url = "https://github.com/norbjd/protoc-gen-jsonschema",
        strip_prefix = "protoc-gen-jsonschema-{version}",
        sha256 = "ba3e313b10a1b50a6c1232d994c13f6e23d3669be4ae7fea13762f42bb3b2abc",
        version = "7680e4998426e62b6896995ff73d4d91cc5fb13c",
        urls = ["https://github.com/norbjd/protoc-gen-jsonschema/archive/{version}.zip"],
        use_category = ["build"],
        release_date = "2023-05-30",
    ),
    dev_cel = dict(
        project_name = "CEL",
        project_desc = "Common Expression Language -- specification and binary representation",
        project_url = "https://github.com/google/cel-spec",
        strip_prefix = "cel-spec-{version}",
        sha256 = "3ee09eb69dbe77722e9dee23dc48dc2cd9f765869fcf5ffb1226587c81791a0b",
        version = "0.15.0",
        urls = ["https://github.com/google/cel-spec/archive/v{version}.tar.gz"],
        use_category = ["api"],
        release_date = "2024-03-27",
    ),
    rules_proto_grpc = dict(
        project_name = "rules_proto_grpc",
        project_desc = "Bazel rules for building Protobuf and gRPC code and libraries from proto_library targets ",
        project_url = "https://github.com/rules-proto-grpc/rules_proto_grpc",
        version = "4.6.0",
        sha256 = "2a0860a336ae836b54671cbbe0710eec17c64ef70c4c5a88ccfd47ea6e3739bd",
        strip_prefix = "rules_proto_grpc-{version}",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/{version}/rules_proto_grpc-{version}.tar.gz"],
        use_category = ["build"],
        release_date = "2023-12-14",
    ),
    envoy_toolshed = dict(
        project_name = "envoy_toolshed",
        project_desc = "Tooling, libraries, runners and checkers for Envoy proxy's CI",
        project_url = "https://github.com/envoyproxy/toolshed",
        version = "0.1.4",
        sha256 = "7ddfd251a89518b97c4eb8064a7d37454bbd998bf29e4cd3ad8f44227b5ca7b3",
        strip_prefix = "toolshed-bazel-v{version}/bazel",
        urls = ["https://github.com/envoyproxy/toolshed/archive/bazel-v{version}.tar.gz"],
        use_category = ["build"],
        release_date = "2024-07-22",
        cpe = "N/A",
        license = "Apache-2.0",
        license_url = "https://github.com/envoyproxy/envoy/blob/bazel-v{version}/LICENSE",
    ),
)
