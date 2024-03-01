load("@bazel_gazelle//:deps.bzl", "go_repository")
load("@envoy_api//bazel:repositories.bzl", "api_dependencies")

def _non_module_dependencies_impl(_ctx):
    api_dependencies()
    go_repository(
        name = "com_github_planetscale_vtprotobuf",
        importpath = "github.com/planetscale/vtprotobuf",
        sum = "h1:nve54OLsoKDQhb8ZnnHHUyvAK3vjBiwTEJeC3UsqzJ8=",
        version = "v0.5.1-0.20231205081218-d930d8ac92f8",
        build_external = "external",
    )

non_module_dependencies = module_extension(
    implementation = _non_module_dependencies_impl,
)
