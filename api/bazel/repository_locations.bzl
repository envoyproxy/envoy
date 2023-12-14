# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_skylib = dict(
        project_name = "bazel-skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.4.2",
        sha256 = "66ffd9315665bfaafc96b52278f57c7e2dd09f5ede279ea6d39b2be471e7e3aa",
        release_date = "2023-05-31",
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
        sha256 = "0b1b1ea8c248dce8c7592dc1a93e4adebd116f0d68123f8eb34251e7ce410866",
        version = "1.0.2",
        urls = ["https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v{version}.zip"],
        strip_prefix = "protoc-gen-validate-{version}",
        release_date = "2023-06-26",
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
    com_github_cncf_xds = dict(
        project_name = "xDS API",
        project_desc = "xDS API Working Group (xDS-WG)",
        project_url = "https://github.com/cncf/xds",
        # During the UDPA -> xDS migration, we aren't working with releases.
        version = "3a472e524827f72d1ad621c4983dd5af54c46776",
        sha256 = "dc305e20c9fa80822322271b50aa2ffa917bf4fd3973bcec52bfc28dc32c5927",
        release_date = "2023-11-16",
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
        version = "114a745b2841a044e98cdbb19358ed29fcf4a5f1",
        sha256 = "9b4e0d0a04a217c06b426aefd03b82581a9510ca766d2d1c70e52bb2ad4a0703",
        release_date = "2023-01-10",
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
        version = "0.5.0",
        sha256 = "170873e0b91cab5da6634af1498b88876842ff3e01212e2dabf6b4e6512c948d",
        release_date = "2023-10-03",
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
        version = "4.0.0",
        sha256 = "66bfdf8782796239d3875d37e7de19b1d94301e8972b3cbd2446b332429b4df1",
        release_date = "2021-09-15",
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
        version = "1.0.0",
        sha256 = "a13a1a7b76a1f22a0ca2e6c293e176ffef031413ab8ba653a82a1dbc286a3a33",
        release_date = "2023-07-03",
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
        version = "1.28.1",
        sha256 = "870cf492d381a967d36636fdee9da44b524ea62aad163659b8dbf16a7da56987",
        strip_prefix = "buf",
        urls = ["https://github.com/bufbuild/buf/releases/download/v{version}/buf-Linux-x86_64.tar.gz"],
        release_date = "2023-11-15",
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
    envoy_toolshed = dict(
        project_name = "envoy_toolshed",
        project_desc = "Tooling, libraries, runners and checkers for Envoy proxy's CI",
        project_url = "https://github.com/envoyproxy/toolshed",
        version = "0.1.1",
        sha256 = "ee759b57270a2747f3f2a3d6ecaad63b834dd9887505a9f1c919d72429dbeffd",
        strip_prefix = "toolshed-bazel-v{version}/bazel",
        urls = ["https://github.com/envoyproxy/toolshed/archive/bazel-v{version}.tar.gz"],
        use_category = ["build"],
        release_date = "2023-10-21",
        cpe = "N/A",
        license = "Apache-2.0",
        license_url = "https://github.com/envoyproxy/envoy/blob/bazel-v{version}/LICENSE",
    ),
)
