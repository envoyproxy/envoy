# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_skylib = dict(
        project_name = "bazel-skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.2.1",
        sha256 = "f7be3474d42aae265405a592bb7da8e171919d74c16f082a5457840f06054728",
        release_date = "2022-03-10",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        project_name = "protoc-gen-validate (PGV)",
        project_desc = "protoc plugin to generate polyglot message validators",
        project_url = "https://github.com/envoyproxy/protoc-gen-validate",
        version = "0.6.7",
        sha256 = "4c692c62e16c168049bca2b2972b0a25222870cf53e61be30b50d761e58728bd",
        release_date = "2022-03-04",
        strip_prefix = "protoc-gen-validate-{version}",
        urls = ["https://github.com/envoyproxy/protoc-gen-validate/archive/v{version}.tar.gz"],
        use_category = ["api"],
        implied_untracked_deps = [
            "com_github_iancoleman_strcase",
            "com_github_lyft_protoc_gen_star",
            "com_github_spf13_afero",
            "org_golang_google_genproto",
            "org_golang_x_text",
        ],
    ),
    com_github_cncf_udpa = dict(
        project_name = "xDS API",
        project_desc = "xDS API Working Group (xDS-WG)",
        project_url = "https://github.com/cncf/xds",
        # During the UDPA -> xDS migration, we aren't working with releases.
        version = "1e77728a1eaa11d6c931ec2ccd6e95f516a7ef94",
        sha256 = "57d8a0d8c8412dbe31dae34b576498c5f8b1fd523a2fcd4f5e5154870b043fe8",
        release_date = "2022-05-20",
        strip_prefix = "xds-{version}",
        urls = ["https://github.com/cncf/xds/archive/{version}.tar.gz"],
        use_category = ["api"],
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
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Starlark macro for importing Google API proto.
        project_name = "Google APIs",
        project_desc = "Public interface definitions of Google APIs",
        project_url = "https://github.com/googleapis/googleapis",
        version = "82944da21578a53b74e547774cf62ed31a05b841",
        sha256 = "a45019af4d3290f02eaeb1ce10990166978c807cb33a9692141a076ba46d1405",
        release_date = "2019-12-02",
        strip_prefix = "googleapis-{version}",
        urls = ["https://github.com/googleapis/googleapis/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    opencensus_proto = dict(
        project_name = "OpenCensus Proto",
        project_desc = "Language Independent Interface Types For OpenCensus",
        project_url = "https://github.com/census-instrumentation/opencensus-proto",
        version = "0.3.0",
        sha256 = "b7e13f0b4259e80c3070b583c2f39e53153085a6918718b1c710caf7037572b0",
        release_date = "2020-07-21",
        strip_prefix = "opencensus-proto-{version}/src",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/v{version}.tar.gz"],
        use_category = ["api"],
    ),
    prometheus_metrics_model = dict(
        project_name = "Prometheus client model",
        project_desc = "Data model artifacts for Prometheus",
        project_url = "https://github.com/prometheus/client_model",
        version = "147c58e9608a4f9628b53b6cc863325ca746f63a",
        sha256 = "f7da30879dcdfae367fa65af1969945c3148cfbfc462b30b7d36f17134675047",
        release_date = "2021-06-07",
        strip_prefix = "client_model-{version}",
        urls = ["https://github.com/prometheus/client_model/archive/{version}.tar.gz"],
        use_category = ["api"],
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
    ),
    opentelemetry_proto = dict(
        project_name = "OpenTelemetry Proto",
        project_desc = "Language Independent Interface Types For OpenTelemetry",
        project_url = "https://github.com/open-telemetry/opentelemetry-proto",
        version = "0.11.0",
        sha256 = "985367f8905e91018e636cbf0d83ab3f834b665c4f5899a27d10cae9657710e2",
        release_date = "2021-10-07",
        strip_prefix = "opentelemetry-proto-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-proto/archive/v{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_github_bufbuild_buf = dict(
        project_name = "buf",
        project_desc = "A new way of working with Protocol Buffers.",  # Used for breaking change detection in API protobufs
        project_url = "https://buf.build",
        version = "1.5.0",
        sha256 = "4e28ac1c5e75d35595f4437ad6e1006a65c131db5528b682cd6c8561212c523c",
        strip_prefix = "buf",
        urls = ["https://github.com/bufbuild/buf/releases/download/v{version}/buf-Linux-x86_64.tar.gz"],
        release_date = "2022-05-30",
        use_category = ["api"],
        tags = ["manual"],
    ),
)
