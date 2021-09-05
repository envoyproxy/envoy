
def _impl(repository_ctx):
    repository_ctx.file("WORKSPACE", "")
    repository_ctx.file("BUILD", "")
    version = repository_ctx.read(repository_ctx.path(repository_ctx.attr.envoy_root).dirname.get_child("VERSION"))
    repository_ctx.file("version.bzl", """VERSION = '%s'""" % version.strip())
    repository_ctx.file("path.bzl", """PATH = '%s'""" % repository_ctx.path(repository_ctx.attr.envoy_root).dirname)


envoy_repo = repository_rule(
    implementation=_impl,
    attrs = {
        "envoy_root": attr.label(default = "@envoy//:BUILD"),
    }
)


def envoy_repo_binding():
    if "envoy_repo" not in native.existing_rules().keys():
        envoy_repo(name = "envoy_repo")
