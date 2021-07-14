# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_skylib = dict(
        project_name = "bazel-skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.0.3",
        sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
        release_date = "2020-08-27",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        project_name = "protoc-gen-validate (PGV)",
        project_desc = "protoc plugin to generate polyglot message validators",
        project_url = "https://github.com/envoyproxy/protoc-gen-validate",
        version = "0.6.1",
        sha256 = "c695fc5a2e5a1b52904cd8a58ce7a1c3a80f7f50719496fd606e551685c01101",
        release_date = "2021-04-26",
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
    com_github_bazelbuild_buildtools = dict(
        project_name = "Bazel build tools",
        project_desc = "Developer tools for working with Google's bazel buildtool.",
        project_url = "https://github.com/bazelbuild/buildtools",
        version = "4.0.1",
        sha256 = "c28eef4d30ba1a195c6837acf6c75a4034981f5b4002dda3c5aa6e48ce023cf1",
        release_date = "2021-03-01",
        strip_prefix = "buildtools-{version}",
        urls = ["https://github.com/bazelbuild/buildtools/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    com_github_cncf_udpa = dict(
        project_name = "xDS API",
        project_desc = "xDS API Working Group (xDS-WG)",
        project_url = "https://github.com/cncf/xds",
        # During the UDPA -> xDS migration, we aren't working with releases.
        version = "b4d4f75666b03252210862d1d61a08567a22f36d",
        sha256 = "d79c3aaca0b14518661dc62c333d2e9fe1c4e8ff179eaf33d5b125cc78f892d8",
        release_date = "2021-03-23",
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
        version = "f7a30f6f80006b591fa7c437fe5a951eb10bcbcf",
        sha256 = "9fc210a34f0f9e7cc31598d109b5d069ef44911a82f507d5a88716db171615a8",
        release_date = "2021-02-09",
        strip_prefix = "rules_proto-{version}",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/{version}.tar.gz"],
        use_category = ["api"],
    ),
    opentelemetry_proto = dict(
        project_name = "OpenTelemetry Proto",
        project_desc = "Language Independent Interface Types For OpenTelemetry",
        project_url = "https://github.com/open-telemetry/opentelemetry-proto",
        version = "0.9.0",
        sha256 = "9ec38ab51eedbd7601979b0eda962cf37bc8a4dc35fcef604801e463f01dcc00",
        release_date = "2021-05-12",
        strip_prefix = "opentelemetry-proto-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-proto/archive/v{version}.tar.gz"],
        use_category = ["api"],
    ),
)
