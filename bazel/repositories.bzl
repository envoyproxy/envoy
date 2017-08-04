load(":target_recipes.bzl", "TARGET_RECIPES")
load(":repository_locations.bzl", "REPO_LOCATIONS")

def _repository_impl(ctxt):
    # Setup the build directory with links to the relevant files.
    ctxt.symlink(Label("//bazel:repositories.sh"), "repositories.sh")
    ctxt.symlink(Label("//ci/build_container:build_and_install_deps.sh"),
                 "build_and_install_deps.sh")
    ctxt.symlink(Label("//ci/build_container:recipe_wrapper.sh"), "recipe_wrapper.sh")
    ctxt.symlink(Label("//ci/build_container:Makefile"), "Makefile")
    for r in ctxt.attr.recipes:
        ctxt.symlink(Label("//ci/build_container/build_recipes:" + r + ".sh"),
                     "build_recipes/" + r + ".sh")
    ctxt.symlink(Label("//ci/prebuilt:BUILD"), "BUILD")

    # Run the build script.
    environment = {}
    print("Fetching external dependencies...")
    result = ctxt.execute(
        ["./repositories.sh"] + ctxt.attr.recipes,
        environment = environment,
        quiet = False,
    )
    print("External dep build exited with return code: %d" % result.return_code)
    print(result.stdout)
    print(result.stderr)
    if result.return_code != 0:
        fail("External dep build failed")

def _protobuf_repository_impl(ctxt):
    deps_path = ctxt.attr.envoy_deps_path
    if not deps_path.endswith("/"):
        deps_path += "/"
    ctxt.symlink(Label(deps_path + "thirdparty/protobuf:protobuf.bzl"), "protobuf.bzl")
    ctxt.symlink(Label(deps_path + "thirdparty/protobuf:BUILD"), "BUILD")
    ctxt.symlink(ctxt.path(Label(deps_path + "thirdparty/protobuf:BUILD")).dirname.get_child("src"),
                 "src")

def py_jinja2_dep():
    BUILD = """
py_library(
    name = "jinja2",
    srcs = glob(["jinja2/**/*.py"]),
    visibility = ["//visibility:public"],
    deps = ["@markupsafe_git//:markupsafe"],
)
"""
    native.new_git_repository(
        name = "jinja2_git",
        remote = REPO_LOCATIONS["jinja2"],
        tag = "2.9.6",
        build_file_content = BUILD,
    )

def py_markupsafe_dep():
    BUILD = """
py_library(
    name = "markupsafe",
    srcs = glob(["markupsafe/**/*.py"]),
    visibility = ["//visibility:public"],
)
"""
    native.new_git_repository(
        name = "markupsafe_git",
        remote = REPO_LOCATIONS["markupsafe"],
        tag = "1.0",
        build_file_content = BUILD,
    )

# Python dependencies. If these become non-trivial, we might be better off using a virtualenv to
# wrap them, but for now we can treat them as first-class Bazel.
def python_deps(skip_targets):
    if 'markupsafe' not in skip_targets:
        py_markupsafe_dep()
        native.bind(
            name = "markupsafe",
            actual = "@markupsafe_git//:markupsafe",
        )
    if 'jinja2' not in skip_targets:
        py_jinja2_dep()
        native.bind(
            name = "jinja2",
            actual = "@jinja2_git//:jinja2",
        )

def cc_grpc_httpjson_transcoding_dep():
    native.git_repository(
        name = "grpc_httpjson_transcoding",
        remote = REPO_LOCATIONS["grpc_transcoding"],
        commit = "e4f58aa07b9002befa493a0a82e10f2e98b51fc6",
    )

# Bazel native C++ dependencies. For the depedencies that doesn't provide autoconf/automake builds.
def cc_deps(skip_targets):
    if 'grpc-httpjson-transcoding' not in skip_targets:
        cc_grpc_httpjson_transcoding_dep()
        native.bind(
            name = "path_matcher",
            actual = "@grpc_httpjson_transcoding//src:path_matcher",
        )
        native.bind(
            name = "grpc_transcoding",
            actual = "@grpc_httpjson_transcoding//src:transcoding",
        )

def envoy_api_deps(skip_targets):
  if 'envoy_api' not in skip_targets:
    native.git_repository(
        name = "envoy_api",
        remote = REPO_LOCATIONS["envoy_api"],
        commit = "cff546dceebbd6893c96bc9c88266732f4f76d9b",
    )
    bind_targets = [
        "address",
        "base",
        "bootstrap",
        "cds",
        "eds",
        "health_check",
        "protocol",
        "rds",
        "tls_context",
    ]
    for t in bind_targets:
        native.bind(
            name = "envoy_" + t,
            actual = "@envoy_api//api:" + t + "_cc",
        )
    native.bind(
        name = "http_api_protos",
        actual = "@googleapis//:http_api_protos",
    )
    native.bind(
        name = "http_api_protos_genproto",
        actual = "@googleapis//:http_api_protos_genproto",
    )

def envoy_dependencies(path = "@envoy_deps//", skip_protobuf_bzl = False, skip_targets = []):
    native.bind(
        name = "cc_wkt_protos",
        actual = "@protobuf_bzl//:cc_wkt_protos",
    )
    native.bind(
        name = "cc_wkt_protos_genproto",
        actual = "@protobuf_bzl//:cc_wkt_protos_genproto",
    )

    envoy_repository = repository_rule(
        implementation = _repository_impl,
        environ = [
            "CC",
            "CXX",
            "LD_LIBRARY_PATH"
        ],
        # Don't pretend we're in the sandbox, we do some evil stuff with envoy_dep_cache.
        local = True,
        attrs = {
            "recipes": attr.string_list(),
        },
    )

    # Ideally, we wouldn't have a single repository target for all dependencies, but instead one per
    # dependency, as suggested in #747. However, it's much faster to build all deps under a single
    # recursive make job and single make jobserver.
    recipes = depset()
    for t in TARGET_RECIPES:
        if t not in skip_targets:
            recipes += depset([TARGET_RECIPES[t]])

    envoy_repository(
        name = "envoy_deps",
        recipes = recipes.to_list(),
    )

    protobuf_repository = repository_rule(
        implementation = _protobuf_repository_impl,
        attrs = {
            "envoy_deps_path": attr.string(),
        },
    )

    # If the WORKSPACE hasn't already supplied @protobuf_bzl and told us to skip it, we need to map in the
    # full repo into @protobuf_bzl so that we can depend on this in envoy_build_system.bzl and for things
    # like @protobuf_bzl//:cc_wkt_protos in envoy-api. We do this by some evil symlink stuff.
    if not skip_protobuf_bzl:
        protobuf_repository(
            name = "protobuf_bzl",
            envoy_deps_path = path,
        )

    for t in TARGET_RECIPES:
        if t not in skip_targets:
            native.bind(
                name = t,
                actual = path + ":" + t,
            )

    python_deps(skip_targets)
    cc_deps(skip_targets)
    envoy_api_deps(skip_targets)
