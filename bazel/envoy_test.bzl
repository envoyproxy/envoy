# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy test targets. This includes both test library and test binary targets.
load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load("@rules_fuzzing//fuzzing:cc_defs.bzl", "fuzzing_decoration")
load(":envoy_binary.bzl", "envoy_cc_binary")
load(":envoy_library.bzl", "tcmalloc_external_deps")
load(":envoy_pch.bzl", "envoy_pch_copts")
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_dbg_linkopts",
    "envoy_exported_symbols_input",
    "envoy_external_dep_path",
    "envoy_linkstatic",
    "envoy_select_exported_symbols",
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
        alwayslink = 1,
        disable_pch = False,
        **kargs):
    # Add implicit tcmalloc external dependency(if available) in order to enable CPU and heap profiling in tests.
    deps += tcmalloc_external_deps(repository)
    extra_deps = []
    pch_copts = []
    if disable_pch:
        extra_deps = [envoy_external_dep_path("googletest")]
    else:
        extra_deps = [repository + "//test:test_pch"]
        pch_copts = envoy_pch_copts(repository, "//test:test_pch")

    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        data = data,
        copts = envoy_copts(repository, test = True) + copts + pch_copts,
        testonly = 1,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + extra_deps,
        tags = tags,
        include_prefix = include_prefix,
        alwayslink = alwayslink,
        linkstatic = envoy_linkstatic(),
        **kargs
    )

# Compute the test linkopts based on various options.
def _envoy_test_linkopts():
    return select({
        "@envoy//bazel:apple": [],
        "@envoy//bazel:windows_x86_64": [
            "-DEFAULTLIB:ws2_32.lib",
            "-DEFAULTLIB:iphlpapi.lib",
            "-WX",
        ],

        # TODO(mattklein123): It's not great that we universally link against the following libs.
        # In particular, -latomic and -lrt are not needed on all platforms. Make this more granular.
        "//conditions:default": ["-pthread", "-lrt", "-ldl"],
    }) + envoy_select_force_libcpp([], ["-lstdc++fs", "-latomic"]) + envoy_dbg_linkopts() + envoy_select_exported_symbols(["-Wl,-E"])

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
        corpus_name = name + "_corpus_files"
        native.filegroup(
            name = corpus_name,
            srcs = native.glob([corpus + "/**"]),
        )
    else:
        corpus_name = corpus

    test_lib_name = name + "_lib"
    envoy_cc_test_library(
        name = test_lib_name,
        deps = deps + envoy_stdlib_deps() + [
            repository + "//test/fuzz:fuzz_runner_lib",
            repository + "//test/test_common:test_version_linkstamp",
        ],
        repository = repository,
        tags = tags,
        **kwargs
    )

    native.cc_test(
        name = name,
        copts = envoy_copts("@envoy", test = True),
        additional_linker_inputs = envoy_exported_symbols_input(),
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
            "//conditions:default": [
                ":" + test_lib_name,
                "@envoy//bazel:fuzzing_engine",
            ],
        }),
        size = size,
        tags = ["fuzz_target"] + tags,
    )

    fuzzing_decoration(
        name = name,
        raw_binary = name,
        engine = "@envoy//bazel:fuzzing_engine",
        corpus = [corpus_name],
        dicts = dictionaries,
        define_regression_test = False,
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
        condition = None,
        shard_count = None,
        coverage = True,
        local = False,
        size = "medium",
        flaky = False,
        env = {},
        exec_properties = {}):
    coverage_tags = tags + ([] if coverage else ["nocoverage"])

    native.cc_test(
        name = name,
        srcs = srcs,
        data = data,
        copts = envoy_copts(repository, test = True) + copts + envoy_pch_copts(repository, "//test:test_pch"),
        additional_linker_inputs = envoy_exported_symbols_input(),
        linkopts = _envoy_test_linkopts(),
        linkstatic = envoy_linkstatic(),
        malloc = tcmalloc_external_dep(repository),
        deps = envoy_stdlib_deps() + deps + [envoy_external_dep_path(dep) for dep in external_deps + ["googletest"]] + [
            repository + "//test:main",
            repository + "//test/test_common:test_version_linkstamp",
        ] + select({
            repository + "//bazel:clang_pch_build": [repository + "//test:test_pch"],
            "//conditions:default": [],
        }),
        # from https://github.com/google/googletest/blob/6e1970e2376c14bf658eb88f655a054030353f9f/googlemock/src/gmock.cc#L51
        # 2 - by default, mocks act as StrictMocks.
        args = args + ["--gmock_default_mock_behavior=2"],
        tags = coverage_tags,
        local = local,
        shard_count = shard_count,
        size = size,
        flaky = flaky,
        env = env,
        exec_properties = exec_properties,
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
        alwayslink = 1,
        **kargs):
    disable_pch = kargs.pop("disable_pch", True)
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
        alwayslink = alwayslink,
        disable_pch = disable_pch,
        **kargs
    )

# Envoy test binaries should be specified with this function.
def envoy_cc_test_binary(
        name,
        tags = [],
        deps = [],
        **kargs):
    envoy_cc_binary(
        name,
        testonly = 1,
        linkopts = _envoy_test_linkopts(),
        tags = tags + ["compilation_db_dep"],
        deps = deps + [
            "@envoy//test/test_common:test_version_linkstamp",
        ],
        **kargs
    )

# Envoy benchmark binaries should be specified with this function. bazel run
# these targets to measure performance.
def envoy_cc_benchmark_binary(
        name,
        deps = [],
        repository = "",
        **kargs):
    envoy_cc_test_binary(
        name,
        deps = deps + [repository + "//test/benchmark:main"],
        repository = repository,
        **kargs
    )

# Tests to validate that Envoy benchmarks run successfully should be specified
# with this function. Not for actual performance measurements: iteratons and
# expensive benchmarks will be skipped in the interest of execution time.
def envoy_benchmark_test(
        name,
        benchmark_binary,
        data = [],
        tags = [],
        repository = "",
        **kargs):
    native.sh_test(
        name = name,
        srcs = [repository + "//bazel:test_for_benchmark_wrapper.sh"],
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

# Envoy py_tests should be specified with this function.
def envoy_py_test(
        name,
        external_deps = [],
        deps = [],
        **kargs):
    py_test(
        name = name,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps],
        **kargs
    )

# Envoy C++ mock targets should be specified with this function.
def envoy_cc_mock(name, **kargs):
    envoy_cc_test_library(name = name, disable_pch = True, **kargs)

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
