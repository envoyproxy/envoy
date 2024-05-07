load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("//:versions.bzl", "VERSIONS")

def load_github_archives():
    for k, v in VERSIONS.items():
        if type(v) == type("") or v.get("type") != "github_archive":
            continue
        kwargs = dict(name = k, **v)
        http_archive(
            **{
                k: (
                    (v.format(**kwargs) if not k.startswith("patch") else v)
                    if type(v) == "string"
                    else [
                            _v.format(**kwargs) if not k.startswith("patch") else _v
                            for _v in v
                    ]
                )
                for k, v in kwargs.items()
                if k not in ["repo", "type", "version"]
            }
        )

def load_archives():
    load_github_archives()
