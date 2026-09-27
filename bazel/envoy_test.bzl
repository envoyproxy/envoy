load("@envoy_repo//:compiler.bzl", "LLVM_PATH")

# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy test targets. This includes both test library and test binary targets.
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_fuzzing//fuzzing:cc_defs.bzl", "fuzzing_decoration")
load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load(":envoy_binary.bzl", "envoy_cc_binary")
load(
    ":envoy_internal.bzl",
    "envoy_copts",
    "envoy_dbg_linkopts",
    "envoy_exported_symbols_input",
    "envoy_external_dep_path",
    "envoy_linkstatic",
    "envoy_select_force_libcpp",
    "envoy_stdlib_deps",
    "tcmalloc_external_dep",
    "tcmalloc_external_deps",
)
load(":envoy_pch.bzl", "envoy_pch_copts", "envoy_pch_deps")
load(":envoy_select.bzl", "deprecate_repository")

_APPLE = Label("//bazel:apple")
_ASAN_BUILD = Label("//bazel:asan_build")
_BENCHMARK_MAIN_LIB = Label("//test/benchmark:main_lib")
_BENCHMARK_MAIN_SRC = Label("//test/benchmark:main.cc")
_ENABLE_EXPORTED_SYMBOLS = Label("//bazel:enable_exported_symbols")
_ENGFLOW_RBE_X86_64 = Label("//bazel:engflow_rbe_x86_64")
_EXPORTED_SYMBOLS = Label("//bazel:exported_symbols.txt")
_EXPORTED_SYMBOLS_APPLE = Label("//bazel:exported_symbols_apple.txt")
_FUZZING_ENGINE = Label("//bazel:fuzzing_engine")
_LIBFUZZER = Label("//bazel:libfuzzer")
_LIBFUZZER_COVERAGE = Label("//bazel:libfuzzer_coverage")
_LINUX = Label("//bazel:linux")
_LOCAL_ASAN_BUILD = Label("//bazel:local_asan_build")
_TEST_DUMMY_MAIN = Label("//test:dummy_main")
_TEST_MAIN = Label("//bazel:test_main")
_TEST_PCH = Label("//bazel:test_pch")
_TEST_VERSION_LINKSTAMP = Label("//test/test_common:test_version_linkstamp")
_WINDOWS_X86_64 = Label("//bazel:windows_x86_64")

# Envoy C++ related test infrastructure (that want gtest, gmock, but may be
# relied on by envoy_cc_test_library) should use this function.
def _envoy_cc_test_infrastructure_library(
        name,
        srcs = [],
        hdrs = [],
        data = [],
        external_deps = [],
        deps = [],
        tags = [],
        include_prefix = None,
        copts = [],
        alwayslink = 1,
        disable_pch = False,
        **kargs):
    # Add implicit tcmalloc external dependency(if available) in order to enable CPU and heap profiling in tests.
    deps += tcmalloc_external_deps()
    extra_deps = []
    pch_copts = []
    if disable_pch:
        extra_deps = ["@googletest//:gtest"]
    else:
        extra_deps = envoy_pch_deps(_TEST_PCH)
        pch_copts = envoy_pch_copts(_TEST_PCH)

    cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        data = data,
        copts = envoy_copts(test = True) + copts + pch_copts,
        testonly = 1,
        deps = deps + [envoy_external_dep_path(dep) for dep in external_deps] + extra_deps,
        tags = tags,
        include_prefix = include_prefix,
        alwayslink = alwayslink,
        linkstatic = envoy_linkstatic(),
        **kargs
    )

def _envoy_test_default_exported_symbols():
    return select({
        _LINUX: [
            "-Wl,--dynamic-list=$(location %s)" % str(_EXPORTED_SYMBOLS),
        ],
        _APPLE: [
            "-Wl,-exported_symbols_list,$(location %s)" % str(_EXPORTED_SYMBOLS_APPLE),
        ],
        "//conditions:default": [],
    })

# Select the given values if exporting is enabled in the current build.
def envoy_test_select_exported_symbols(xs):
    return select({
        _ENABLE_EXPORTED_SYMBOLS: xs,
        "//conditions:default": [],
    })

# Compute the test linkopts based on various options.
def _envoy_test_linkopts():
    return select({
        _APPLE: [],
        _WINDOWS_X86_64: [
            "-DEFAULTLIB:ws2_32.lib",
            "-DEFAULTLIB:iphlpapi.lib",
            "-DEFAULTLIB:Bcrypt.lib",
            "-WX",
        ],

        # TODO(mattklein123): It's not great that we universally link against the following libs.
        # In particular, -latomic and -lrt are not needed on all platforms. Make this more granular.
        "//conditions:default": ["-pthread", "-lrt", "-ldl"],
    }) + envoy_select_force_libcpp([], ["-lstdc++fs", "-latomic"]) + envoy_dbg_linkopts() + envoy_test_select_exported_symbols(["-Wl,-E"])

# Envoy C++ fuzz test targets. These are not included in coverage runs.
def envoy_cc_fuzz_test(
        name,
        corpus,
        dictionaries = [],
        rbe_pool = None,
        exec_properties = {},
        repository = "",
        size = "medium",
        deps = [],
        tags = [],
        **kwargs):
    exec_properties = exec_properties | select({
        _ENGFLOW_RBE_X86_64: {"Pool": rbe_pool} if rbe_pool else {},
        "//conditions:default": {},
    })
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
        exec_properties = exec_properties,
        deps = deps + envoy_stdlib_deps() + [
            Label("//test/fuzz:fuzz_runner_lib"),
            _TEST_VERSION_LINKSTAMP,
        ],
        tags = tags,
        **kwargs
    )

    cc_test(
        name = name,
        copts = envoy_copts(test = True),
        additional_linker_inputs = envoy_exported_symbols_input(),
        linkopts = _envoy_test_linkopts() + select({
            # `@llvm_toolchain` links libc++, keep -fsanitize=fuzzer from also linking libstdc++.
            _LIBFUZZER: ["-fsanitize=fuzzer", "-nostdlib++"],
            "//conditions:default": [],
        }),
        linkstatic = envoy_linkstatic(),
        args = select({
            _LIBFUZZER_COVERAGE: ["$(locations %s)" % corpus_name],
            _LIBFUZZER: [],
            "//conditions:default": ["$(locations %s)" % corpus_name],
        }),
        data = [corpus_name],
        exec_properties = exec_properties,
        # No fuzzing on macOS or Windows
        deps = select({
            _APPLE: [_TEST_DUMMY_MAIN],
            _WINDOWS_X86_64: [_TEST_DUMMY_MAIN],
            "//conditions:default": [
                ":" + test_lib_name,
                _FUZZING_ENGINE,
            ],
        }) + deprecate_repository("envoy_cc_fuzz_test", repository),
        size = size,
        tags = ["fuzz_target"] + tags,
    )

    fuzzing_decoration(
        name = name,
        raw_binary = name,
        engine = _FUZZING_ENGINE,
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
        linkopts = [],
        condition = None,
        shard_count = None,
        coverage = True,
        local = False,
        size = "medium",
        flaky = False,
        env = {},
        rbe_pool = None,
        exec_properties = {}):
    coverage_tags = tags + ([] if coverage else ["nocoverage"])
    exec_properties = exec_properties | select({
        _ENGFLOW_RBE_X86_64: {"Pool": rbe_pool} if rbe_pool else {},
        "//conditions:default": {},
    })
    cc_test(
        name = name,
        srcs = srcs,
        data = data + select({
            _LOCAL_ASAN_BUILD: [],
            _ASAN_BUILD: ["@llvm_toolchain_llvm//:symbolizer"],
            "//conditions:default": [],
        }),
        copts = envoy_copts(test = True) + copts + envoy_pch_copts(_TEST_PCH),
        additional_linker_inputs = envoy_exported_symbols_input(),
        linkopts = _envoy_test_linkopts() + linkopts,
        linkstatic = envoy_linkstatic(),
        malloc = tcmalloc_external_dep(),
        deps = envoy_stdlib_deps() + deps + [envoy_external_dep_path(dep) for dep in external_deps] + [
            _TEST_MAIN,
            _TEST_VERSION_LINKSTAMP,
            "@googletest//:gtest",
        ] + envoy_pch_deps(_TEST_PCH) + deprecate_repository("envoy_cc_test", repository),
        # from https://github.com/google/googletest/blob/6e1970e2376c14bf658eb88f655a054030353f9f/googlemock/src/gmock.cc#L51
        # 2 - by default, mocks act as StrictMocks.
        args = args + ["--gmock_default_mock_behavior=2"],
        tags = coverage_tags,
        local = local,
        shard_count = shard_count,
        size = size,
        flaky = flaky,
        env = env | select({
            _LOCAL_ASAN_BUILD: {"ASAN_SYMBOLIZER_PATH": "%s/bin/llvm-symbolizer" % LLVM_PATH},
            _ASAN_BUILD: {"ASAN_SYMBOLIZER_PATH": "$(location @llvm_toolchain_llvm//:symbolizer)"},
            "//conditions:default": {},
        }),
        exec_properties = exec_properties,
    )

# Envoy C++ test targets loading dynamic modules should be specified with this macro.
def envoy_cc_dyn_module_test(
        name,
        **kargs):
    envoy_cc_test(
        name,
        linkopts = _envoy_test_default_exported_symbols(),
        **kargs
    )

# Envoy C++ test related libraries (that want gtest, gmock) should be specified
# with this function.
def envoy_cc_test_library(
        name,
        srcs = [],
        hdrs = [],
        data = [],
        rbe_pool = None,
        exec_properties = {},
        external_deps = [],
        deps = [],
        repository = "",
        tags = [],
        include_prefix = None,
        copts = [],
        alwayslink = 1,
        **kargs):
    exec_properties = exec_properties | select({
        _ENGFLOW_RBE_X86_64: {"Pool": rbe_pool} if rbe_pool else {},
        "//conditions:default": {},
    })
    disable_pch = kargs.pop("disable_pch", True)
    _envoy_cc_test_infrastructure_library(
        name,
        srcs,
        hdrs,
        data,
        external_deps,
        deps + deprecate_repository("envoy_cc_test_library", repository),
        tags,
        include_prefix,
        copts,
        visibility = ["//visibility:public"],
        alwayslink = alwayslink,
        disable_pch = disable_pch,
        exec_properties = exec_properties,
        **kargs
    )

# Envoy test binaries should be specified with this function.
def envoy_cc_test_binary(
        name,
        tags = [],
        deps = [],
        linkopts = [],
        stamp = 0,
        linkstatic = True,
        **kargs):
    envoy_cc_binary(
        name,
        testonly = 1,
        linkopts = _envoy_test_linkopts() + linkopts,
        tags = tags + ["compilation_db_dep"],
        deps = deps + [
            _TEST_VERSION_LINKSTAMP,
        ],
        stamp = stamp,
        linkstatic = linkstatic,
        **kargs
    )

# Envoy benchmark binaries should be specified with this function. bazel run
# these targets to measure performance.
#
# Callers must list `@benchmark` in `deps`; the macro deliberately does not
# inject it so that downstream consumers declare their own benchmark dependency.
def envoy_cc_benchmark_binary(
        name,
        srcs = [],
        deps = [],
        repository = "",
        **kargs):
    envoy_cc_test_binary(
        name,
        srcs = srcs + [_BENCHMARK_MAIN_SRC],
        # `@tclap` intentionally resolves in the caller's repo mapping so downstream
        # bzlmod consumers must declare it in their MODULE.bazel.
        deps = deps + [_BENCHMARK_MAIN_LIB, "@tclap"] + deprecate_repository("envoy_cc_benchmark_binary", repository),
        **kargs
    )

# Envoy benchmark binaries loading dynamic modules should be specified with this function. bazel run
# these targets to measure performance.
def envoy_cc_benchmark_dyn_module_binary(
        name,
        srcs = [],
        deps = [],
        repository = "",
        **kargs):
    envoy_cc_test_binary(
        name,
        srcs = srcs + [_BENCHMARK_MAIN_SRC],
        deps = deps + [_BENCHMARK_MAIN_LIB, "@tclap"] + deprecate_repository("envoy_cc_benchmark_dyn_module_binary", repository),
        linkopts = _envoy_test_default_exported_symbols(),
        **kargs
    )

# Tests to validate that Envoy benchmarks run successfully should be specified
# with this function. Not for actual performance measurements: iteratons and
# expensive benchmarks will be skipped in the interest of execution time.
def envoy_benchmark_test(
        name,
        benchmark_binary,
        data = [],
        rbe_pool = None,
        exec_properties = {},
        tags = [],
        repository = "",
        **kargs):
    exec_properties = exec_properties | select({
        _ENGFLOW_RBE_X86_64: {"Pool": rbe_pool} if rbe_pool else {},
        "//conditions:default": {},
    })
    sh_test(
        name = name,
        srcs = [Label("//bazel:test_for_benchmark_wrapper.sh")],
        deps = ["@bazel_tools//tools/bash/runfiles"] + deprecate_repository("envoy_benchmark_test", repository),
        data = [":" + benchmark_binary] + data,
        exec_properties = exec_properties,
        args = ["$(rlocationpath %s)" % native.package_relative_label(benchmark_binary)],
        tags = tags + ["no_san", "nocoverage"],
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
        sh_test(
            name = name,
            srcs = ["//bazel:sh_test_wrapper.sh"],
            data = srcs + data + cc_binary,
            args = srcs,
            tags = tags + ["nocoverage"],
            **kargs
        )
