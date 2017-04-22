load(":target_recipes.bzl", "TARGET_RECIPES")

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
        # Ideally we would print progress, but instead this hangs on "INFO: Loading
        # complete.  Analyzing.." today, see
        # https://github.com/bazelbuild/bazel/issues/1289. We could set quiet=False
        # as well to indicate progress, but that isn't supported in versions folks
        # are using right now (0.4.5).
        # TODO(htuch): Revisit this when everyone is on newer Bazel versions.
        #
        # quiet = False,
    )
    print("External dep build exited with return code: %d" % result.return_code)
    print(result.stdout)
    print(result.stderr)
    if result.return_code != 0:
        fail("External dep build failed")

def py_jinja2_dep():
    BUILD = """
py_library(
    name = "jinja2",
    srcs = glob(["jinja2/**/*.py"]),
    visibility = ["//visibility:public"],
)
"""
    native.new_git_repository(
        name = "jinja2_git",
        remote = "https://github.com/pallets/jinja.git",
        tag = "2.9.6",
        build_file_content = BUILD,
    )

# Python dependencies. If these become non-trivial, we might be better off using a virtualenv to
# wrap them, but for now we can treat them as first-class Bazel.
def python_deps(skip_targets):
    if 'jinja2' not in skip_targets:
        py_jinja2_dep()
        native.bind(
            name = "jinja2",
            actual = "@jinja2_git//:jinja2",
        )

def envoy_dependencies(path = "@envoy_deps//", skip_protobuf_bzl = False, skip_targets = []):
    # Used only for protobuf.bzl.
    if not skip_protobuf_bzl:
        native.new_git_repository(
            name = "protobuf_bzl",
            # Using a non-canonical repository/branch here. This is a workaround to the lack of
            # merge on https://github.com/google/protobuf/pull/2508, which is needed for supporting
            # arbitrary CC compiler locations from the environment. The branch is
            # https://github.com/htuch/protobuf/tree/v3.2.0-default-shell-env, which is the 3.2.0
            # release with the above mentioned PR cherry picked.
            commit = "d490587268931da78c942a6372ef57bb53db80da",
            remote = "https://github.com/htuch/protobuf.git",
            # We only want protobuf.bzl, so don't support building out of this repo.
            build_file_content = "",
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

    for t in TARGET_RECIPES:
        if t not in skip_targets:
            native.bind(
                name = t,
                actual = path + ":" + t,
            )

    python_deps(skip_targets)
