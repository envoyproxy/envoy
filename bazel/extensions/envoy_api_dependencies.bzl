load("@envoy_api//bazel:repositories.bzl", "external_http_archive")

def _envoy_api_dependencies_impl(_ctx):
    external_http_archive("com_github_grpc_grpc")
    external_http_archive("com_google_googleapis")
    external_http_archive("envoy_toolshed")
    external_http_archive("rules_proto_grpc")
    

envoy_api_dependencies_extension = module_extension(
    implementation = _envoy_api_dependencies_impl,
)
