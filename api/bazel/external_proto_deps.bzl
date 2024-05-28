# Any external dependency imported in the api/ .protos requires entries in
# the maps below, to allow the Bazel proto and language specific bindings to be
# inferred from the import directives.
#
# This file needs to be interpreted as both Python 3 and Starlark, so only the
# common subset of Python should be used.

# This maps from .proto import directive path to the Bazel dependency path for
# external dependencies. Since BUILD files are generated, this is the canonical
# place to define this mapping.
EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP = {
    "google/api/expr/v1alpha1/checked.proto": "@com_google_googleapis//google/api/expr/v1alpha1:checked_proto",
    "google/api/expr/v1alpha1/syntax.proto": "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto",
    "io/prometheus/client/metrics.proto": "@prometheus_metrics_model//:client_model",
    "opencensus/proto/trace/v1/trace.proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto",
    "opencensus/proto/trace/v1/trace_config.proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto",
    "opentelemetry/proto/common/v1/common.proto": "@opentelemetry_proto//:common",
}

# This maps from the Bazel proto_library target to the Go language binding target for external dependencies.
EXTERNAL_PROTO_GO_BAZEL_DEP_MAP = {
    # Note @com_google_googleapis are point to @go_googleapis.
    #
    # It is aligned to xDS dependency to suppress the conflicting package heights error between
    # @com_github_cncf_xds//xds/type/matcher/v3:pkg_go_proto
    # @envoy_api//envoy/config/rbac/v3:pkg_go_proto
    #
    # TODO(https://github.com/bazelbuild/rules_go/issues/1986): update to
    #    @com_google_googleapis when the bug is resolved. Also see the note to
    #    go_googleapis in https://github.com/bazelbuild/rules_go/blob/master/go/dependencies.rst#overriding-dependencies
    "@com_google_googleapis//google/api/expr/v1alpha1:checked_proto": "@org_golang_google_genproto_googleapis_api//expr/v1alpha1",
    "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto": "@org_golang_google_genproto_googleapis_api//expr/v1alpha1",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_go",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_and_config_proto_go",
    "@opentelemetry_proto//:trace": "@opentelemetry_proto//:trace_go_proto",
    "@opentelemetry_proto//:logs": "@opentelemetry_proto//:logs_go_proto",
    "@opentelemetry_proto//:metrics": "@opentelemetry_proto//:metrics_go_proto",
    "@opentelemetry_proto//:common": "@opentelemetry_proto//:common_go_proto",
}

# This maps from the Bazel proto_library target to the C++ language binding target for external dependencies.
EXTERNAL_PROTO_CC_BAZEL_DEP_MAP = {
    "@com_google_googleapis//google/api/expr/v1alpha1:checked_proto": "@com_google_googleapis//google/api/expr/v1alpha1:checked_cc_proto",
    "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto": "@com_google_googleapis//google/api/expr/v1alpha1:syntax_cc_proto",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_cc",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto_cc",
    "@opentelemetry_proto//:trace": "@opentelemetry_proto//:trace_cc_proto",
    "@opentelemetry_proto//:logs": "@opentelemetry_proto//:logs_cc_proto",
    "@opentelemetry_proto//:metrics": "@opentelemetry_proto//:metrics_cc_proto",
    "@opentelemetry_proto//:common": "@opentelemetry_proto//:common_cc_proto",
}
