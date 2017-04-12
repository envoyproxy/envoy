load(":recipes.bzl", "RECIPES")

def _repository_impl(ctxt):
    # Setup the build directory with links to the relevant files.
    ctxt.symlink(Label("//bazel:repositories.sh"), "repositories.sh")
    ctxt.symlink(Label("//ci/build_container:build_and_install_deps.sh"),
                 "build_and_install_deps.sh")
    ctxt.symlink(Label("//ci/build_container:recipes.bzl"), "recipes.bzl")
    ctxt.symlink(Label("//ci/build_container:print_recipe_deps.sh"), "print_recipe_deps.sh")
    ctxt.symlink(Label("//ci/build_container:Makefile"), "Makefile")
    for r in RECIPES:
        ctxt.symlink(Label("//ci/build_container/build_recipes:" + r + ".sh"),
                     "build_recipes/" + r + ".sh")
    ctxt.symlink(Label("//ci/prebuilt:BUILD"), "BUILD")

    # Run the build script.
    environment = {}
    if ctxt.attr.debug:
        environment["DEBUG"] = "1"
    result = ctxt.execute(
        ["./repositories.sh"],
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
    if result.return_code != 0 or ctxt.attr.debug:
        print("External dep build exited with return code: %d" % result.return_code)
        print(result.stdout)
        print(result.stderr)
        if result.return_code != 0:
            fail("External dep build failed")

def envoy_dependencies(path = "@envoy_deps//", local_protobuf_bzl = None):
    # Used only for protobuf.bzl.
    if local_protobuf_bzl:
        native.new_local_repository(
            name = "protobuf_bzl",
            path = "/thirdparty/protobuf-3.2.0",
            # We only want protobuf.bzl, so don't support building out of this repo.
            build_file_content = "",
        )
    else:
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

    # Set this to True to make the build debug cycles faster.
    debug_build = False

    envoy_repository = repository_rule(
        implementation = _repository_impl,
        local = debug_build,
        environ = ["CC", "CXX", "LD_LIBRARY_PATH"],
        attrs = {"debug": attr.bool(default=False)},
    )

    envoy_repository(
        name = "envoy_deps",
        debug = debug_build,
    )

    native.bind(
        name = "ares",
        actual = path + ":ares",
    )

    native.bind(
        name = "cc_wkt_protos_genproto",
        actual = path + ":cc_wkt_protos_genproto",
    )

    native.bind(
        name = "cc_wkt_protos",
        actual = path + ":cc_wkt_protos",
    )

    native.bind(
        name = "event",
        actual = path + ":event",
    )

    native.bind(
        name = "event_pthreads",
        actual = path + ":event_pthreads",
    )

    native.bind(
        name = "googletest",
        actual = path + ":googletest",
    )

    native.bind(
        name = "http_parser",
        actual = path + ":http_parser",
    )

    native.bind(
        name = "lightstep",
        actual = path + ":lightstep",
    )

    native.bind(
        name = "nghttp2",
        actual = path + ":nghttp2",
    )

    native.bind(
        name = "protobuf",
        actual = path + ":protobuf",
    )

    native.bind(
        name = "protoc",
        actual = path + ":protoc",
    )

    native.bind(
        name = "rapidjson",
        actual = path + ":rapidjson",
    )

    native.bind(
        name = "spdlog",
        actual = path + ":spdlog",
    )

    native.bind(
        name = "ssl",
        actual = path + ":ssl",
    )

    native.bind(
        name = "tclap",
        actual = path + ":tclap",
    )
