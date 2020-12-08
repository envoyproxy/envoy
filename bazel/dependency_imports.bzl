load("@bazel_gazelle//:deps.bzl", "gazelle_dependencies", "go_repository")
load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict", "custom_exec_properties")
load("@build_bazel_rules_apple//apple:repositories.bzl", "apple_rules_dependencies")
load("@envoy_build_tools//toolchains:rbe_toolchains_config.bzl", "rbe_toolchains_config")
load("@io_bazel_rules_go//go:deps.bzl", "go_register_toolchains", "go_rules_dependencies")
load("@io_bazel_rules_rust//rust:repositories.bzl", "rust_repositories")
load("@proxy_wasm_cpp_host//bazel/cargo:crates.bzl", "proxy_wasm_cpp_host_raze__fetch_remote_crates")
load("@proxy_wasm_rust_sdk//bazel:dependencies.bzl", "proxy_wasm_rust_sdk_dependencies")
load("@rules_antlr//antlr:deps.bzl", "antlr_dependencies")
load("@rules_foreign_cc//:workspace_definitions.bzl", "rules_foreign_cc_dependencies")
load("@rules_python//python:pip.bzl", "pip_install")
load("@upb//bazel:repository_defs.bzl", upb_bazel_version_repository = "bazel_version_repository")

# go version for rules_go
GO_VERSION = "1.14.7"

def envoy_dependency_imports(go_version = GO_VERSION):
    apple_rules_dependencies()
    go_register_toolchains(go_version)
    gazelle_dependencies()
    go_rules_dependencies()
    proxy_wasm_cpp_host_raze__fetch_remote_crates()
    proxy_wasm_rust_sdk_dependencies()
    rbe_toolchains_config()
    rules_foreign_cc_dependencies()
    rust_repositories()
    upb_bazel_version_repository(name = "upb_bazel_version")
    antlr_dependencies(472)

    custom_exec_properties(
        name = "envoy_large_machine_exec_property",
        constants = {
            "LARGE_MACHINE": create_rbe_exec_properties_dict(labels = dict(size = "large")),
        },
    )

    # These dependencies, like most of the Go in this repository, exist only for the API.
    go_repository(
        name = "org_golang_google_grpc",
        build_file_proto_mode = "disable",
        importpath = "google.golang.org/grpc",
        sum = "h1:DGeFlSan2f+WEtCERJ4J9GJWk15TxUi8QGagfI87Xyc=",
        version = "v1.33.1",
        # project_url = "https://pkg.go.dev/google.golang.org/grpc",
        # last_update = "2020-10-21"
        # use_category = ["api"],
        # cpe = "cpe:2.3:a:grpc:grpc:*",
    )
    go_repository(
        name = "org_golang_x_net",
        importpath = "golang.org/x/net",
        sum = "h1:eBmm0M9fYhWpKZLjQUUKka/LtIxf46G4fxeEz5KJr9U=",
        version = "v0.0.0-20201202161906-c7110b5ffcbb",
        # project_url = "https://pkg.go.dev/mod/golang.org/x/net",
        # last_update = "2020-12-02"
        # use_category = ["api"],
    )
    go_repository(
        name = "org_golang_x_text",
        importpath = "golang.org/x/text",
        sum = "h1:0YWbFKbhXG/wIiuHDSKpS0Iy7FSA+u45VtBMfQcFTTc=",
        version = "v0.3.4",
        # project_url = "https://pkg.go.dev/mod/golang.org/x/text",
        # last_update = "2020-10-27"
        # use_category = ["api"],
    )

    pip_install(
        name = "config_validation_pip3",
        requirements = "@envoy//tools/config_validation:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "PyYAML",
        # project_url = "https://github.com/yaml/pyyaml",
        # version = "5.3.1",
        # last_update = "2020-03-18"
        # use_category = ["devtools"],
        # cpe = "cpe:2.3:a:pyyaml:pyyaml:*",
    )
    pip_install(
        name = "configs_pip3",
        requirements = "@envoy//configs:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Jinja",
        # project_url = "http://palletsprojects.com/p/jinja",
        # version = "2.11.2",
        # last_update = "2020-04-13"
        # use_category = ["test"],
        # cpe = "cpe:2.3:a:palletsprojects:jinja:*",

        # project_name = "MarkupSafe",
        # project_url = "https://markupsafe.palletsprojects.com/en/1.1.x/",
        # version = "1.1.1",
        # last_update = "2019-02-23"
        # use_category = ["test"],
    )
    pip_install(
        name = "kafka_pip3",
        requirements = "@envoy//source/extensions/filters/network/kafka:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Jinja",
        # project_url = "http://palletsprojects.com/p/jinja",
        # version = "2.11.2",
        # last_update = "2020-04-13"
        # use_category = ["test"],
        # cpe = "cpe:2.3:a:palletsprojects:jinja:*",

        # project_name = "MarkupSafe",
        # project_url = "https://markupsafe.palletsprojects.com/en/1.1.x/",
        # version = "1.1.1",
        # last_update = "2019-02-23"
        # use_category = ["test"],
    )
    pip_install(
        name = "headersplit_pip3",
        requirements = "@envoy//tools/envoy_headersplit:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Clang",
        # project_url = "https://clang.llvm.org/",
        # version = "10.0.1",
        # last_update = "2020-07-21"
        # use_category = ["devtools"],
        # cpe = "cpe:2.3:a:llvm:clang:*",
    )
    pip_install(
        name = "protodoc_pip3",
        requirements = "@envoy//tools/protodoc:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "PyYAML",
        # project_url = "https://github.com/yaml/pyyaml",
        # version = "5.3.1",
        # last_update = "2020-03-18"
        # use_category = ["devtools"],
        # cpe = "cpe:2.3:a:pyyaml:pyyaml:*",
    )
    pip_install(
        name = "thrift_pip3",
        requirements = "@envoy//test/extensions/filters/network/thrift_proxy:requirements.txt",
        extra_pip_args = ["--require-hashes"],

        # project_name = "Apache Thrift",
        # project_url = "http://thrift.apache.org/",
        # version = "0.11.0",
        # last_update = "2017-12-07"
        # use_category = ["dataplane"],
        # cpe = "cpe:2.3:a:apache:thrift:*",

        # project_name = "Six: Python 2 and 3 Compatibility Library",
        # project_url = "https://six.readthedocs.io/",
        # version = "1.15.0",
        # last_update = "2020-05-21"
        # use_category = ["dataplane"],
    )
