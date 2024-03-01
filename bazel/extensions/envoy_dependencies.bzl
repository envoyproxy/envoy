load("@envoy//bazel:repositories.bzl", "envoy_dependencies")
load("@envoy_api//bazel:envoy_http_archive.bzl", "envoy_http_archive")

def _envoy_dependencies_impl(_ctx):
    envoy_http_archive("com_google_googleapis")
    envoy_dependencies()

envoy_dependencies_extension = module_extension(
    implementation = _envoy_dependencies_impl,
)
