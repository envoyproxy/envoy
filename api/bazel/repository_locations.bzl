# This should match the schema defined in external_deps.bzl.
REPOSITORY_LOCATIONS_SPEC = dict(
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
    googleapis = dict(
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
    com_github_chrusty_protoc_gen_jsonschema = dict(
        strip_prefix = "protoc-gen-jsonschema-{version}",
        sha256 = "ba3e313b10a1b50a6c1232d994c13f6e23d3669be4ae7fea13762f42bb3b2abc",
        version = "7680e4998426e62b6896995ff73d4d91cc5fb13c",
        urls = ["https://github.com/norbjd/protoc-gen-jsonschema/archive/{version}.zip"],
    ),
)
