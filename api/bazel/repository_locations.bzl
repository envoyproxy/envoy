DEPENDENCY_REPOSITORIES_SPEC = dict(
    bazel_skylib = dict(
        project_name = "bazel-skylib",
        project_desc = "Common useful functions and rules for Bazel",
        project_url = "https://github.com/bazelbuild/bazel-skylib",
        version = "1.0.3",
        sha256 = "1c531376ac7e5a180e0237938a2536de0c54d93f5c278634818e0efc952dd56c",
        urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/{version}/bazel-skylib-{version}.tar.gz"],
        last_updated = "2020-08-27",
        use_category = ["api"],
    ),
    com_envoyproxy_protoc_gen_validate = dict(
        project_name = "protoc-gen-validate (PGV)",
        project_desc = "protoc plugin to generate polyglot message validators",
        project_url = "https://github.com/envoyproxy/protoc-gen-validate",
        version = "278964a8052f96a2f514add0298098f63fb7f47f",
        sha256 = "e368733c9fb7f8489591ffaf269170d7658cc0cd1ee322b601512b769446d3c8",
        strip_prefix = "protoc-gen-validate-{version}",
        urls = ["https://github.com/envoyproxy/protoc-gen-validate/archive/{version}.tar.gz"],
        last_updated = "2020-06-09",
        use_category = ["api"],
    ),
    com_github_cncf_udpa = dict(
        project_name = "Universal Data Plane API",
        project_desc = "Universal Data Plane API Working Group (UDPA-WG)",
        project_url = "https://github.com/cncf/udpa",
        version = "0.0.1",
        sha256 = "83a7dcc316d741031f34c0409021432b74a39c4811845a177133f02f948fe2d8",
        strip_prefix = "udpa-{version}",
        urls = ["https://github.com/cncf/udpa/archive/v{version}.tar.gz"],
        last_updated = "2020-09-23",
        use_category = ["api"],
    ),
    com_github_openzipkin_zipkinapi = dict(
        project_name = "Zipkin API",
        project_desc = "Zipkin's language independent model and HTTP Api Definitions",
        project_url = "https://github.com/openzipkin/zipkin-api",
        version = "0.2.2",
        sha256 = "688c4fe170821dd589f36ec45aaadc03a618a40283bc1f97da8fa11686fc816b",
        strip_prefix = "zipkin-api-{version}",
        urls = ["https://github.com/openzipkin/zipkin-api/archive/{version}.tar.gz"],
        last_updated = "2020-09-23",
        use_category = ["api"],
    ),
    com_google_googleapis = dict(
        # TODO(dio): Consider writing a Starlark macro for importing Google API proto.
        project_name = "Google APIs",
        project_desc = "Public interface definitions of Google APIs",
        project_url = "https://github.com/googleapis/googleapis",
        version = "82944da21578a53b74e547774cf62ed31a05b841",
        sha256 = "a45019af4d3290f02eaeb1ce10990166978c807cb33a9692141a076ba46d1405",
        strip_prefix = "googleapis-{version}",
        urls = ["https://github.com/googleapis/googleapis/archive/{version}.tar.gz"],
        last_updated = "2019-12-02",
        use_category = ["api"],
    ),
    opencensus_proto = dict(
        project_name = "OpenCensus Proto",
        project_desc = "Language Independent Interface Types For OpenCensus",
        project_url = "https://github.com/census-instrumentation/opencensus-proto",
        version = "0.3.0",
        sha256 = "b7e13f0b4259e80c3070b583c2f39e53153085a6918718b1c710caf7037572b0",
        strip_prefix = "opencensus-proto-{version}/src",
        urls = ["https://github.com/census-instrumentation/opencensus-proto/archive/v{version}.tar.gz"],
        last_updated = "2020-06-20",
        use_category = ["api"],
    ),
    prometheus_metrics_model = dict(
        project_name = "Prometheus client model",
        project_desc = "Data model artifacts for Prometheus",
        project_url = "https://github.com/prometheus/client_model",
        version = "60555c9708c786597e6b07bf846d0dc5c2a46f54",
        sha256 = "6748b42f6879ad4d045c71019d2512c94be3dd86f60965e9e31e44a3f464323e",
        strip_prefix = "client_model-{version}",
        urls = ["https://github.com/prometheus/client_model/archive/{version}.tar.gz"],
        last_updated = "2020-06-23",
        use_category = ["api"],
    ),
    rules_proto = dict(
        project_name = "Protobuf Rules for Bazel",
        project_desc = "Protocol buffer rules for Bazel",
        project_url = "https://github.com/bazelbuild/rules_proto",
        version = "40298556293ae502c66579620a7ce867d5f57311",
        sha256 = "aa1ee19226f707d44bee44c720915199c20c84a23318bb0597ed4e5c873ccbd5",
        strip_prefix = "rules_proto-{version}",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/{version}.tar.gz"],
        last_updated = "2020-08-17",
        use_category = ["api"],
    ),
)

def _format_version(s, version):
    return s.format(version = version, dash_version = version.replace(".", "-"), underscore_version = version.replace(".", "_"))

# Interpolate {version} in the above dependency specs. This code should be capable of running in both Python
# and Starlark.
def _dependency_repositories():
    locations = {}
    for key, location in DEPENDENCY_REPOSITORIES_SPEC.items():
        mutable_location = dict(location)
        locations[key] = mutable_location

        # Fixup with version information.
        if "version" in location:
            if "strip_prefix" in location:
                mutable_location["strip_prefix"] = _format_version(location["strip_prefix"], location["version"])
            mutable_location["urls"] = [_format_version(url, location["version"]) for url in location["urls"]]
    return locations

REPOSITORY_LOCATIONS = _dependency_repositories()
