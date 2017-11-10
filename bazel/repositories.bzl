load(":genrule_repository.bzl", "genrule_repository")
load(":patched_http_archive.bzl", "patched_http_archive")
load(":repository_locations.bzl", "REPO_LOCATIONS")
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

def go_deps(skip_targets):
    if 'io_bazel_rules_go' not in skip_targets:
        native.git_repository(
            name = "io_bazel_rules_go",
            remote = "https://github.com/bazelbuild/rules_go.git",
            commit = "4374be38e9a75ff5957c3922adb155d32086fe14",
        )

def envoy_api_deps(skip_targets):
  if 'envoy_api' not in skip_targets:
    native.git_repository(
        name = "envoy_api",
        remote = REPO_LOCATIONS["envoy_api"],
        commit = "b236cc7c73353959af4b70903273d358c10aeb34",
    )

    api_bind_targets = [
        "address",
        "base",
        "bootstrap",
        "discovery",
        "cds",
        "discovery",
        "eds",
        "health_check",
        "lds",
        "protocol",
        "rds",
        "sds",
    ]
    for t in api_bind_targets:
        native.bind(
            name = "envoy_" + t,
            actual = "@envoy_api//api:" + t + "_cc",
        )
    filter_bind_targets = [
        "accesslog",
        "fault",
    ]
    for t in filter_bind_targets:
        native.bind(
            name = "envoy_filter_" + t,
            actual = "@envoy_api//api/filter:" + t + "_cc",
        )
    http_filter_bind_targets = [
        "http_connection_manager",
        "router",
        "buffer",
        "transcoder",
        "rate_limit",
        "ip_tagging",
        "health_check",
        "fault",
    ]
    for t in http_filter_bind_targets:
        native.bind(
            name = "envoy_filter_http_" + t,
            actual = "@envoy_api//api/filter/http:" + t + "_cc",
        )
    network_filter_bind_targets = [
        "tcp_proxy",
        "mongo_proxy",
        "redis_proxy",
        "rate_limit",
        "client_ssl_auth",
    ]
    for t in network_filter_bind_targets:
        native.bind(
            name = "envoy_filter_network_" + t,
            actual = "@envoy_api//api/filter/network:" + t + "_cc",
        )    
    native.bind(
        name = "http_api_protos",
        actual = "@googleapis//:http_api_protos",
    )
    native.bind(
        name = "http_api_protos_lib",
        actual = "@googleapis//:http_api_protos_lib",
    )

def envoy_dependencies(path = "@envoy_deps//", skip_com_google_protobuf = False, skip_targets = [],
                       repository = ""):
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

    # `existing_rule_keys` contains the names of repositories that have already
    # been defined in the Bazel workspace. By skipping repos with existing keys,
    # users can override dependency versions by using standard Bazel repository
    # rules in their WORKSPACE files.
    #
    # The long repo names (`com_github_fmtlib_fmt` instead of `fmtlib`) are
    # semi-standard in the Bazel community, intended to avoid both duplicate
    # dependencies and name conflicts.
    existing_rule_keys = native.existing_rules().keys()
    if not ("fmtlib" in skip_targets or "com_github_fmtlib_fmt" in existing_rule_keys):
        com_github_fmtlib_fmt(repository)
    if not ("spdlog" in skip_targets or "com_github_gabime_spdlog" in existing_rule_keys):
        com_github_gabime_spdlog(repository)
    if not ("lightstep" in skip_targets or "com_github_lightstep_lightstep_tracer_cpp" in existing_rule_keys):
        com_github_lightstep_lightstep_tracer_cpp(repository)
    if not (skip_com_google_protobuf or "com_google_protobuf" in existing_rule_keys):
        com_google_protobuf()

    for t in TARGET_RECIPES:
        if t not in skip_targets:
            native.bind(
                name = t,
                actual = path + ":" + t,
            )

    python_deps(skip_targets)
    cc_deps(skip_targets)
    go_deps(skip_targets)
    envoy_api_deps(skip_targets)

def com_github_fmtlib_fmt(repository = ""):
  native.new_http_archive(
      name = "com_github_fmtlib_fmt",
      urls = [
          "https://github.com/fmtlib/fmt/releases/download/4.0.0/fmt-4.0.0.zip",
      ],
      sha256 = "10a9f184d4d66f135093a08396d3b0a0ebe8d97b79f8b3ddb8559f75fe4fcbc3",
      strip_prefix = "fmt-4.0.0",
      build_file = repository + "//bazel/external:fmtlib.BUILD",
  )
  native.bind(
      name="fmtlib",
      actual="@com_github_fmtlib_fmt//:fmtlib",
  )

def com_github_gabime_spdlog(repository = ""):
  native.new_http_archive(
      name = "com_github_gabime_spdlog",
      urls = [
          "https://github.com/gabime/spdlog/archive/v0.14.0.tar.gz",
      ],
      sha256 = "eb5beb4e53f4bfff5b32eb4db8588484bdc15a17b90eeefef3a9fc74fec1d83d",
      strip_prefix = "spdlog-0.14.0",
      build_file = repository + "//bazel/external:spdlog.BUILD",
  )
  native.bind(
      name="spdlog",
      actual="@com_github_gabime_spdlog//:spdlog",
  )

def com_github_lightstep_lightstep_tracer_cpp(repository = ""):
  genrule_repository(
      name = "com_github_lightstep_lightstep_tracer_cpp",
      urls = [
          "https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_36/lightstep-tracer-cpp-0.36.tar.gz",
      ],
      sha256 = "f7477e67eca65f904c0b90a6bfec46d58cccfc998a8e75bc3259b6e93157ff84",
      strip_prefix = "lightstep-tracer-cpp-0.36",
      patches = [
          repository + "//bazel/external:lightstep-missing-header.patch",
      ],
      genrule_cmd_file = repository + "//bazel/external:lightstep.genrule_cmd",
      build_file = repository + "//bazel/external:lightstep.BUILD",
  )
  native.bind(
      name="lightstep",
      actual="@com_github_lightstep_lightstep_tracer_cpp//:lightstep",
  )

def com_google_protobuf():
  # TODO(htuch): This can switch back to a point release http_archive at the next
  # release (> 3.4.1), we need HEAD proto_library support and
  # https://github.com/google/protobuf/pull/3761.
  native.http_archive(
      name = "com_google_protobuf",
      strip_prefix = "protobuf-c4f59dcc5c13debc572154c8f636b8a9361aacde",
      sha256 = "5d4551193416861cb81c3bc0a428f22a6878148c57c31fb6f8f2aa4cf27ff635",
      url = "https://github.com/google/protobuf/archive/c4f59dcc5c13debc572154c8f636b8a9361aacde.tar.gz",
  )
  # Needed for cc_proto_library, Bazel doesn't support aliases today for repos,
  # see https://groups.google.com/forum/#!topic/bazel-discuss/859ybHQZnuI and
  # https://github.com/bazelbuild/bazel/issues/3219.
  native.http_archive(
      name = "com_google_protobuf_cc",
      strip_prefix = "protobuf-c4f59dcc5c13debc572154c8f636b8a9361aacde",
      sha256 = "5d4551193416861cb81c3bc0a428f22a6878148c57c31fb6f8f2aa4cf27ff635",
      url = "https://github.com/google/protobuf/archive/c4f59dcc5c13debc572154c8f636b8a9361aacde.tar.gz",
  )
  native.bind(
      name = "protobuf",
      actual = "@com_google_protobuf//:protobuf",
  )
  native.bind(
      name = "protoc",
      actual = "@com_google_protobuf_cc//:protoc",
  )
