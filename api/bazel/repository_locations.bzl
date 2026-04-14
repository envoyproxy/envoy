# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_skylib = dict(
        version = "1.9.0",
        sha256 = "3b5b49006181f5f8ff626ef8ddceaa95e9bb8ad294f7b5d7b11ea9f7ddaf8c59",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        sha256 = "20da84f8efe8f53c10cbde79d2cdd1e63365b0ae1ad9de22af56aa1a49d59330",
        version = "1.3.3",
        urls = ["https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v{version}.zip"],
        strip_prefix = "protoc-gen-validate-{version}",
    ),
    rules_jvm_external = dict(
        version = "6.10",
        strip_prefix = "rules_jvm_external-{version}",
        sha256 = "e5f83b8f2678d2b26441e5eafefb1b061826608417b8d24e5e8e15e585eab1ba",
        urls = ["https://github.com/bazelbuild/rules_jvm_external/releases/download/{version}/rules_jvm_external-{version}.tar.gz"],
    ),
    xds = dict(
        # During the UDPA -> xDS migration, we aren't working with releases.
        version = "8bfbf64dc13ee1a570be4fbdcfccbdd8532463f0",
        sha256 = "82363065ca2c978398d2307fe960c301a0b6655d55981fac017783bba22771d9",
        strip_prefix = "xds-{version}",
        urls = ["https://github.com/cncf/xds/archive/{version}.tar.gz"],
    ),
    zipkin_api = dict(
        version = "1.0.0",
        sha256 = "6c8ee2014cf0746ba452e5f2c01f038df60e85eb2d910b226f9aa27ddc0e44cf",
        strip_prefix = "zipkin-api-{version}",
        urls = ["https://github.com/openzipkin/zipkin-api/archive/{version}.tar.gz"],
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Starlark macro for importing Google API proto.
        version = "fd52b5754b2b268bc3a22a10f29844f206abb327",
        sha256 = "97fc354dddfd3ea03e7bf2ad74129291ed6fad7ff39d3bd8daec738a3672eb8a",
        strip_prefix = "googleapis-{version}",
        urls = ["https://github.com/googleapis/googleapis/archive/{version}.tar.gz"],
    ),
    prometheus_metrics_model = dict(
        version = "0.6.2",
        sha256 = "47c5ea7949f68e7f7b344350c59b6bd31eeb921f0eec6c3a566e27cf1951470c",
        strip_prefix = "client_model-{version}",
        urls = ["https://github.com/prometheus/client_model/archive/v{version}.tar.gz"],
    ),
    rules_buf = dict(
        version = "0.5.2",
        sha256 = "19d845cedf32c0e74a01af8d0bd904872bddc7905f087318d00b332aa36d3929",
        strip_prefix = "rules_buf-{version}",
        urls = ["https://github.com/bufbuild/rules_buf/archive/refs/tags/v{version}.tar.gz"],
    ),
    rules_proto = dict(
        version = "7.1.0",
        sha256 = "14a225870ab4e91869652cfd69ef2028277fc1dc4910d65d353b62d6e0ae21f4",
        strip_prefix = "rules_proto-{version}",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/refs/tags/{version}.tar.gz"],
    ),
    opentelemetry_proto = dict(
        version = "1.10.0",
        sha256 = "52c85df79badc45da7e6a8735e8090b05a961b0208756187e1492a40db2d1f5f",
        strip_prefix = "opentelemetry-proto-{version}",
        urls = ["https://github.com/open-telemetry/opentelemetry-proto/archive/v{version}.tar.gz"],
    ),
    com_github_chrusty_protoc_gen_jsonschema = dict(
        strip_prefix = "protoc-gen-jsonschema-{version}",
        sha256 = "ba3e313b10a1b50a6c1232d994c13f6e23d3669be4ae7fea13762f42bb3b2abc",
        version = "7680e4998426e62b6896995ff73d4d91cc5fb13c",
        urls = ["https://github.com/norbjd/protoc-gen-jsonschema/archive/{version}.zip"],
    ),
    dev_cel = dict(
        strip_prefix = "cel-spec-{version}",
        sha256 = "13583c5a312861648449845b709722676a3c9b43396b6b8e9cbe4538feb74ad2",
        version = "0.25.1",
        urls = ["https://github.com/google/cel-spec/archive/v{version}.tar.gz"],
    ),
    envoy_toolshed = dict(
        version = "0.3.32",
        sha256 = "c97a787075422f9748ff3cf28eb160b8ef8b17e27c44083205ed6473d3945ae9",
        strip_prefix = "toolshed-bazel-v{version}",
        urls = ["https://github.com/envoyproxy/toolshed/releases/download/bazel-v{version}/toolshed-bazel-v{version}.tar.gz"],
    ),
)
