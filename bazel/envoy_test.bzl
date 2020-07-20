load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy test targets. This includes both test library and test binary targets.
load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")
load(":envoy_binary.bzl", "envoy_cc_binary")
load(":envoy_library.bzl", "tcmalloc_external_deps")
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_external_dep_path",
    "envoy_linkstatic",
    "envoy_select_force_libcpp",
    "envoy_stdlib_deps",
    "tcmalloc_external_dep",
)

# Envoy C++ related test infrastructure (that want gtest, gmock, but may be
# relied on by envoy_cc_test_library) should use this function.
def _envoy_cc_test_infrastructure_library(
        name,
        srcs = [],
        hdrs = [],
        data = [],
        external_deps = [],
        deps = [],
        repository = "",
        tags = [],
        include_prefix = None,
        copts = [],
        **kargs):
    # Add implicit tcmalloc external dependency(if available) in order to enable CPU and heap profiling in tests.
    deps += tcmalloc_external_deps(repository)
    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        data = data,
        copts = envoy_copts(repository, test = True) + copts,
        testonly = 1,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            envoy_external_dep_path("googletest"),
        ],
        tags = tags,
        include_prefix = include_prefix,
        alwayslink = 1,
        linkstatic = envoy_linkstatic(),
        **kargs
    )

# Compute the test linkopts based on various options.
def _envoy_test_linkopts():
    return select({
        "@envoy//bazel:apple": [
            # See note here: https://luajit.org/install.html
            "-pagezero_size 10000",
            "-image_base 100000000",
        ],
        "@envoy//bazel:windows_x86_64": [
            "-DEFAULTLIB:advapi32.lib",
            "-DEFAULTLIB:ws2_32.lib",
            "-DEFAULTLIB:iphlpapi.lib",
            "-WX",
        ],

        # TODO(mattklein123): It's not great that we universally link against the following libs.
        # In particular, -latomic and -lrt are not needed on all platforms. Make this more granular.
        "//conditions:default": ["-pthread", "-lrt", "-ldl"],
    }) + envoy_select_force_libcpp([], ["-lstdc++fs", "-latomic"])

# Envoy C++ fuzz test targets. These are not included in coverage runs.
def envoy_cc_fuzz_test(
        name,
        corpus,
        dictionaries = [],
        repository = "",
        size = "medium",
        deps = [],
        tags = [],
        **kwargs):
    if not (corpus.startswith("//") or corpus.startswith(":") or corpus.startswith("@")):
        corpus_name = name + "_corpus"
        corpus = native.glob([corpus + "/**"])
        native.filegroup(
            name = corpus_name,
            srcs = corpus,
        )
    else:
        corpus_name = corpus
    tar_src = [corpus_name]
    if dictionaries:
        tar_src += dictionaries
    pkg_tar(
        name = name + "_corpus_tar",
        srcs = tar_src,
        testonly = 1,
    )
    fuzz_copts = ["-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION"]
    test_lib_name = name + "_lib"
    envoy_cc_test_library(
        name = test_lib_name,
        deps = deps + envoy_stdlib_deps() + [
            repository + "//test/fuzz:fuzz_runner_lib",
        ],
        repository = repository,
        tags = tags,
        **kwargs
    )
    cc_test(
        name = name,
        copts = fuzz_copts + envoy_copts("@envoy", test = True),
        linkopts = _envoy_test_linkopts() + select({
            "@envoy//bazel:libfuzzer": ["-fsanitize=fuzzer"],
            "//conditions:default": [],
        }),
        linkstatic = envoy_linkstatic(),
        args = select({
            "@envoy//bazel:libfuzzer_coverage": ["$(locations %s)" % corpus_name],
            "@envoy//bazel:libfuzzer": [],
            "//conditions:default": ["$(locations %s)" % corpus_name],
        }),
        data = [corpus_name],
        # No fuzzing on macOS or Windows
        deps = select({
            "@envoy//bazel:apple": [repository + "//test:dummy_main"],
            "@envoy//bazel:windows_x86_64": [repository + "//test:dummy_main"],
            "@envoy//bazel:libfuzzer": [
                ":" + test_lib_name,
            ],
            "//conditions:default": [
                ":" + test_lib_name,
                repository + "//test/fuzz:main",
            ],
        }),
        size = size,
        tags = ["fuzz_target"] + tags,
    )

    # This target exists only for
    # https://github.com/google/oss-fuzz/blob/master/projects/envoy/build.sh. It won't yield
    # anything useful on its own, as it expects to be run in an environment where the linker options
    # provide a path to FuzzingEngine.
    cc_binary(
        name = name + "_driverless",
        copts = fuzz_copts + envoy_copts("@envoy", test = True),
        linkopts = ["-lFuzzingEngine"] + _envoy_test_linkopts(),
        linkstatic = 1,
        testonly = 1,
        deps = [":" + test_lib_name],
        tags = ["manual"] + tags,
    )

# Envoy C++ test targets should be specified with this function.
def envoy_cc_test(
        name,
        srcs = [],
        data = [],
        # List of pairs (Bazel shell script target, shell script args)
        repository = "",
        external_deps = [],
        deps = [],
        tags = [],
        args = [],
        copts = [],
        shard_count = None,
        coverage = True,
        local = False,
        size = "medium",
        flaky = False):
    coverage_tags = tags + ([] if coverage else ["nocoverage"])
    cc_test(
        name = name,
        srcs = srcs,
        data = data,
        copts = envoy_copts(repository, test = True) + copts,
        linkopts = _envoy_test_linkopts(),
        linkstatic = envoy_linkstatic(),
        malloc = tcmalloc_external_dep(repository),
        deps = envoy_stdlib_deps() + deps + [envoy_external_dep_path(dep) for dep in external_deps + ["googletest"]] + [
            repository + "//test:main",
        ],
        # from https://github.com/google/googletest/blob/6e1970e2376c14bf658eb88f655a054030353f9f/googlemock/src/gmock.cc#L51
        # 2 - by default, mocks act as StrictMocks.
        args = args + ["--gmock_default_mock_behavior=2"],
        tags = coverage_tags,
        local = local,
        shard_count = shard_count,
        size = size,
        flaky = flaky,
    )

# Envoy C++ test related libraries (that want gtest, gmock) should be specified
# with this function.
def envoy_cc_test_library(
        name,
        srcs = [],
        hdrs = [],
        data = [],
        external_deps = [],
        deps = [],
        repository = "",
        tags = [],
        include_prefix = None,
        copts = [],
        **kargs):
    deps = deps + [
        repository + "//test/test_common:printers_includes",
    ]

    # Same as envoy_cc_library
    srcs += select({
        "@envoy//bazel:compdb_build": ["@envoy//bazel/external:empty.cc"],
        "//conditions:default": [],
    })
    _envoy_cc_test_infrastructure_library(
        name,
        srcs,
        hdrs,
        data,
        external_deps,
        deps,
        repository,
        tags,
        include_prefix,
        copts,
        visibility = ["//visibility:public"],
        **kargs
    )

# Envoy test binaries should be specified with this function.
def envoy_cc_test_binary(
        name,
        tags = [],
        **kargs):
    envoy_cc_binary(
        name,
        testonly = 1,
        linkopts = _envoy_test_linkopts(),
        tags = tags + ["compilation_db_dep"],
        **kargs
    )

# Envoy benchmark binaries should be specified with this function.
def envoy_cc_benchmark_binary(
        name,
        deps = [],
        **kargs):
    envoy_cc_test_binary(
        name,
        deps = deps + ["//test/benchmark:main"],
        **kargs
    )

# Tests to validate that Envoy benchmarks run successfully should be specified with this function.
def envoy_benchmark_test(
        name,
        benchmark_binary,
        data = [],
        tags = [],
        **kargs):
    native.sh_test(
        name = name,
        srcs = ["//bazel:test_for_benchmark_wrapper.sh"],
        data = [":" + benchmark_binary] + data,
        args = ["%s/%s" % (native.package_name(), benchmark_binary)],
        tags = tags + ["nocoverage"],
        **kargs
    )

# Envoy Python test binaries should be specified with this function.
def envoy_py_test_binary(
        name,
        external_deps = [],
        deps = [],
        **kargs):
    py_binary(
        name = name,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps],
        **kargs
    )

# Envoy C++ mock targets should be specified with this function.
def envoy_cc_mock(name, **kargs):
    envoy_cc_test_library(name = name, **kargs)

# Envoy shell tests that need to be included in coverage run should be specified with this function.
def envoy_sh_test(
        name,
        srcs = [],
        data = [],
        coverage = True,
        cc_binary = [],
        tags = [],
        **kargs):
    if coverage:
        if cc_binary == []:
            fail("cc_binary is required for coverage-enabled test.")
        test_runner_cc = name + "_test_runner.cc"
        native.genrule(
            name = name + "_gen_test_runner",
            srcs = srcs,
            outs = [test_runner_cc],
            cmd = "$(location //bazel:gen_sh_test_runner.sh) $(SRCS) >> $@",
            tools = ["//bazel:gen_sh_test_runner.sh"],
        )
        envoy_cc_test(
            name = name,
            srcs = [test_runner_cc],
            data = srcs + data + cc_binary,
            tags = tags,
            deps = ["//test/test_common:environment_lib"] + cc_binary,
            **kargs
        )

    else:
        native.sh_test(
            name = name,
            srcs = ["//bazel:sh_test_wrapper.sh"],
            data = srcs + data + cc_binary,
            args = srcs,
            tags = tags + ["nocoverage"],
            **kargs
        )
