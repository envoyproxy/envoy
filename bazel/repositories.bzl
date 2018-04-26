load(
    "@bazel_tools//tools/build_defs/repo:git.bzl",
    "git_repository",
    "new_git_repository",
)
load(":genrule_repository.bzl", "genrule_repository")
load(":patched_http_archive.bzl", "patched_http_archive")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS")
load(":target_recipes.bzl", "TARGET_RECIPES")

def _repository_impl(name, **kwargs):
    # `existing_rule_keys` contains the names of repositories that have already
    # been defined in the Bazel workspace. By skipping repos with existing keys,
    # users can override dependency versions by using standard Bazel repository
    # rules in their WORKSPACE files.
    existing_rule_keys = native.existing_rules().keys()
    if name in existing_rule_keys:
        # This repository has already been defined, probably because the user
        # wants to override the version. Do nothing.
        return

    location = REPOSITORY_LOCATIONS[name]

    # Git tags are mutable. We want to depend on commit IDs instead. Give the
    # user a useful error if they accidentally specify a tag.
    if "tag" in location:
        fail(
            "Refusing to depend on Git tag %r for external dependency %r: use 'commit' instead."
            % (location["tag"], name))

    if "commit" in location:
        # Git repository at given commit ID. Add a BUILD file if requested.
        if "build_file" in kwargs:
            new_git_repository(
                name = name,
                remote = location["remote"],
                commit = location["commit"],
                **kwargs)
        else:
            git_repository(
                name = name,
                remote = location["remote"],
                commit = location["commit"],
                **kwargs)
    else:  # HTTP
        # HTTP tarball at a given URL. Add a BUILD file if requested.
        if "build_file" in kwargs:
            native.new_http_archive(
                name = name,
                urls = location["urls"],
                sha256 = location["sha256"],
                strip_prefix = location["strip_prefix"],
                **kwargs)
        else:
            native.http_archive(
                name = name,
                urls = location["urls"],
                sha256 = location["sha256"],
                strip_prefix = location["strip_prefix"],
                **kwargs)

def _build_recipe_repository_impl(ctxt):
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
    print(result.stdout)
    print(result.stderr)
    print("External dep build exited with return code: %d" % result.return_code)
    if result.return_code != 0:
        print("\033[31;1m\033[48;5;226m External dependency build failed, check above log " +
              "for errors and ensure all prerequisites at " +
              "https://github.com/envoyproxy/envoy/blob/master/bazel/README.md#quick-start-bazel-build-for-developers are met.")
        # This error message doesn't appear to the user :( https://github.com/bazelbuild/bazel/issues/3683
        fail("External dep build failed")

def _default_envoy_build_config_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("BUILD.bazel", "")
    ctx.symlink(ctx.attr.config, "extensions_build_config.bzl")

_default_envoy_build_config = repository_rule(
    implementation = _default_envoy_build_config_impl,
    attrs = {
        "config": attr.label(default="@envoy//source/extensions:extensions_build_config.bzl"),
    },
)

def _default_envoy_api_impl(ctx):
    ctx.file("WORKSPACE", "")
    ctx.file("BUILD.bazel", "")
    api_dirs = [
        "bazel",
        "docs",
        "envoy",
        "examples",
        "test",
        "tools",
    ]
    for d in api_dirs:
      ctx.symlink(ctx.path(ctx.attr.api).dirname.get_child(d), d)

_default_envoy_api = repository_rule(
    implementation = _default_envoy_api_impl,
    attrs = {
        "api": attr.label(default="@envoy//api:BUILD"),
    },
)

# Python dependencies. If these become non-trivial, we might be better off using a virtualenv to
# wrap them, but for now we can treat them as first-class Bazel.
def _python_deps():
    _repository_impl(
        name = "com_github_pallets_markupsafe",
        build_file = "@envoy//bazel/external:markupsafe.BUILD",
    )
    native.bind(
        name = "markupsafe",
        actual = "@com_github_pallets_markupsafe//:markupsafe",
    )
    _repository_impl(
        name = "com_github_pallets_jinja",
        build_file = "@envoy//bazel/external:jinja.BUILD",
    )
    native.bind(
        name = "jinja2",
        actual = "@com_github_pallets_jinja//:jinja2",
    )

# Bazel native C++ dependencies. For the depedencies that doesn't provide autoconf/automake builds.
def _cc_deps():
    _repository_impl("grpc_httpjson_transcoding")
    native.bind(
        name = "path_matcher",
        actual = "@grpc_httpjson_transcoding//src:path_matcher",
    )
    native.bind(
        name = "grpc_transcoding",
        actual = "@grpc_httpjson_transcoding//src:transcoding",
    )

def _go_deps(skip_targets):
    # Keep the skip_targets check around until Istio Proxy has stopped using
    # it to exclude the Go rules.
    if "io_bazel_rules_go" not in skip_targets:
        _repository_impl("io_bazel_rules_go")

def _envoy_api_deps():
    # Treat the data plane API as an external repo, this simplifies exporting the API to
    # https://github.com/envoyproxy/data-plane-api.
    if "envoy_api" not in native.existing_rules().keys():
        _default_envoy_api(name="envoy_api")

    native.bind(
        name = "http_api_protos",
        actual = "@googleapis//:http_api_protos",
    )
    _repository_impl(
        name = "six_archive",
        build_file = "@com_google_protobuf//:six.BUILD",
    )
    native.bind(
        name = "six",
        actual = "@six_archive//:six",
    )

def envoy_dependencies(path = "@envoy_deps//", skip_targets = []):
    envoy_repository = repository_rule(
        implementation = _build_recipe_repository_impl,
        environ = [
            "CC",
            "CXX",
            "CFLAGS",
            "CXXFLAGS",
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

    # Treat Envoy's overall build config as an external repo, so projects that
    # build Envoy as a subcomponent can easily override the config.
    if "envoy_build_config" not in native.existing_rules().keys():
        _default_envoy_build_config(name = "envoy_build_config")

    # The long repo names (`com_github_fmtlib_fmt` instead of `fmtlib`) are
    # semi-standard in the Bazel community, intended to avoid both duplicate
    # dependencies and name conflicts.
    _boringssl()
    _com_google_absl()
    _com_github_bombela_backward()
    _com_github_circonus_labs_libcircllhist()
    _com_github_cyan4973_xxhash()
    _com_github_eile_tclap()
    _com_github_fmtlib_fmt()
    _com_github_gabime_spdlog()
    _com_github_gcovr_gcovr()
    _com_github_google_libprotobuf_mutator()
    _io_opentracing_cpp()
    _com_lightstep_tracer_cpp()
    _com_github_grpc_grpc()
    _com_github_nodejs_http_parser()
    _com_github_tencent_rapidjson()
    _com_google_googletest()
    _com_google_protobuf()

    # Used for bundling gcovr into a relocatable .par file.
    _repository_impl("subpar")

    _python_deps()
    _cc_deps()
    _go_deps(skip_targets)
    _envoy_api_deps()

def _boringssl():
    _repository_impl("boringssl")
    native.bind(
        name = "ssl",
        actual = "@boringssl//:ssl",
    )

def _com_github_bombela_backward():
    _repository_impl(
        name = "com_github_bombela_backward",
        build_file = "@envoy//bazel/external:backward.BUILD",
    )
    native.bind(
        name = "backward",
        actual = "@com_github_bombela_backward//:backward",
    )

def _com_github_circonus_labs_libcircllhist():
    _repository_impl(
        name = "com_github_circonus_labs_libcircllhist",
        build_file = "@envoy//bazel/external:libcircllhist.BUILD",
    )
    native.bind(
        name = "libcircllhist",
        actual = "@com_github_circonus_labs_libcircllhist//:libcircllhist",
    )

def _com_github_cyan4973_xxhash():
    _repository_impl(
        name = "com_github_cyan4973_xxhash",
        build_file = "@envoy//bazel/external:xxhash.BUILD",
    )
    native.bind(
        name = "xxhash",
        actual = "@com_github_cyan4973_xxhash//:xxhash",
    )

def _com_github_eile_tclap():
    _repository_impl(
        name = "com_github_eile_tclap",
        build_file = "@envoy//bazel/external:tclap.BUILD",
    )
    native.bind(
        name = "tclap",
        actual = "@com_github_eile_tclap//:tclap",
    )

def _com_github_fmtlib_fmt():
    _repository_impl(
        name = "com_github_fmtlib_fmt",
        build_file = "@envoy//bazel/external:fmtlib.BUILD",
    )
    native.bind(
        name = "fmtlib",
        actual = "@com_github_fmtlib_fmt//:fmtlib",
    )

def _com_github_gabime_spdlog():
    _repository_impl(
        name = "com_github_gabime_spdlog",
        build_file = "@envoy//bazel/external:spdlog.BUILD",
    )
    native.bind(
        name = "spdlog",
        actual = "@com_github_gabime_spdlog//:spdlog",
    )

def _com_github_gcovr_gcovr():
    _repository_impl(
        name = "com_github_gcovr_gcovr",
        build_file = "@envoy//bazel/external:gcovr.BUILD",
    )
    native.bind(
        name = "gcovr",
        actual = "@com_github_gcovr_gcovr//:gcovr",
    )

def _com_github_google_libprotobuf_mutator():
    _repository_impl(
        name = "com_github_google_libprotobuf_mutator",
        build_file = "@envoy//bazel/external:libprotobuf_mutator.BUILD",
    )
    native.bind(
        name = "libprotobuf_mutator",
        actual = "@com_github_google_libprotobuf_mutator//:libprotobuf_mutator",
    )

def _io_opentracing_cpp():
    _repository_impl("io_opentracing_cpp")
    native.bind(
        name = "opentracing",
        actual = "@io_opentracing_cpp//:opentracing",
    )

def _com_lightstep_tracer_cpp():
    _repository_impl("com_lightstep_tracer_cpp")
    _repository_impl(
        name = "lightstep_vendored_googleapis",
        build_file = "@com_lightstep_tracer_cpp//:lightstep-tracer-common/third_party/googleapis/BUILD",
    )
    native.bind(
        name = "lightstep",
        actual = "@com_lightstep_tracer_cpp//:lightstep_tracer",
    )

def _com_github_tencent_rapidjson():
    _repository_impl(
        name = "com_github_tencent_rapidjson",
        build_file = "@envoy//bazel/external:rapidjson.BUILD",
    )
    native.bind(
        name = "rapidjson",
        actual = "@com_github_tencent_rapidjson//:rapidjson",
    )

def _com_github_nodejs_http_parser():
    _repository_impl(
        name = "com_github_nodejs_http_parser",
        build_file = "@envoy//bazel/external:http-parser.BUILD",
    )
    native.bind(
        name = "http_parser",
        actual = "@com_github_nodejs_http_parser//:http_parser",
    )

def _com_google_googletest():
    _repository_impl("com_google_googletest")
    native.bind(
        name = "googletest",
        actual = "@com_google_googletest//:gtest",
    )

def _com_google_absl():
    _repository_impl("com_google_absl")
    native.bind(
        name = "abseil_base",
        actual = "@com_google_absl//absl/base:base",
    )
    native.bind(
        name = "abseil_strings",
        actual = "@com_google_absl//absl/strings:strings",
    )
    native.bind(
        name = "abseil_int128",
        actual = "@com_google_absl//absl/numeric:int128",
    )
    native.bind(
        name = "abseil_optional",
        actual = "@com_google_absl//absl/types:optional",
    )

def _com_google_protobuf():
    _repository_impl("com_google_protobuf")

    # Needed for cc_proto_library, Bazel doesn't support aliases today for repos,
    # see https://groups.google.com/forum/#!topic/bazel-discuss/859ybHQZnuI and
    # https://github.com/bazelbuild/bazel/issues/3219.
    location = REPOSITORY_LOCATIONS["com_google_protobuf"]
    native.http_archive(name = "com_google_protobuf_cc", **location)
    native.bind(
        name = "protobuf",
        actual = "@com_google_protobuf//:protobuf",
    )
    native.bind(
        name = "protoc",
        actual = "@com_google_protobuf_cc//:protoc",
    )

def _com_github_grpc_grpc():
    _repository_impl("com_github_grpc_grpc")

    # Rebind some stuff to match what the gRPC Bazel is expecting.
    native.bind(
      name = "protobuf_headers",
      actual = "@com_google_protobuf//:protobuf_headers",
    )
    native.bind(
      name = "libssl",
      actual = "//external:ssl",
    )
    native.bind(
      name = "cares",
      actual = "//external:ares",
    )

    native.bind(
      name = "grpc",
      actual = "@com_github_grpc_grpc//:grpc++"
    )

    native.bind(
      name = "grpc_health_proto",
      actual = "@envoy//bazel:grpc_health_proto",
    )
