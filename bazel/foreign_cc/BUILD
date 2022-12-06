load("//bazel:envoy_build_system.bzl", "envoy_cmake", "envoy_package")
load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

licenses(["notice"])  # Apache 2

exports_files(["icu_data_filter.json"])

envoy_package()

configure_make(
    name = "liburing",
    configure_in_place = True,
    lib_source = "@com_github_axboe_liburing//:all",
    tags = [
        "nocompdb",
        "skip_on_windows",
    ],
)

# autotools packages are unusable on Windows as-is
# TODO: Consider our own gperftools.BUILD file as we do with many other packages
configure_make(
    name = "gperftools_build",
    configure_options = [
        "--enable-shared=no",
        "--enable-frame-pointers",
        "--disable-libunwind",
    ] + select({
        "//bazel:apple": ["AR=/usr/bin/ar"],
        "//conditions:default": [],
    }),
    lib_source = "@com_github_gperftools_gperftools//:all",
    linkopts = ["-lpthread"],
    out_static_libs = select({
        "//bazel:debug_tcmalloc": ["libtcmalloc_debug.a"],
        "//conditions:default": ["libtcmalloc_and_profiler.a"],
    }),
    tags = ["skip_on_windows"],
    targets = [
        "install-libLTLIBRARIES install-perftoolsincludeHEADERS",
    ],
)

# Workaround for https://github.com/bazelbuild/rules_foreign_cc/issues/227
cc_library(
    name = "gperftools",
    tags = ["skip_on_windows"],
    deps = [
        "gperftools_build",
    ],
)

# Kafka client dependency used by Kafka-mesh filter.
# librdkafka build generates extra headers that need to be copied into source to get it to compile.
configure_make(
    name = "librdkafka_build",
    configure_in_place = True,
    configure_options = ["--disable-ssl --disable-gssapi --disable-lz4-ext --disable-zstd && cp Makefile.config src/.. && cp config.h src/.."],
    lib_source = "@edenhill_librdkafka//:all",
    out_static_libs = [
        "librdkafka.a",
        "librdkafka++.a",
    ],
    tags = ["skip_on_windows"],
    targets = [
        "ARFLAGS='' libs install-subdirs",
    ],
    alwayslink = True,
)

cc_library(
    name = "librdkafka",
    tags = ["skip_on_windows"],
    deps = [
        "librdkafka_build",
    ],
)

configure_make(
    name = "luajit",
    configure_command = "build.py",
    env = select({
        # This shouldn't be needed! See
        # https://github.com/envoyproxy/envoy/issues/6084
        # TODO(htuch): Remove when #6084 is fixed
        "//bazel:asan_build": {"ENVOY_CONFIG_ASAN": "1"},
        "//bazel:msan_build": {"ENVOY_CONFIG_MSAN": "1"},
        "//bazel:windows_dbg_build": {"WINDOWS_DBG_BUILD": "debug"},
        "//conditions:default": {},
    }),
    lib_source = "@com_github_luajit_luajit//:all",
    out_include_dir = "include/luajit-2.1",
    out_static_libs = select({
        "//bazel:windows_x86_64": ["lua51.lib"],
        "//conditions:default": ["libluajit-5.1.a"],
    }),
    targets = [],
)

configure_make(
    name = "moonjit",
    configure_command = "build.py",
    env = select({
        # This shouldn't be needed! See
        # https://github.com/envoyproxy/envoy/issues/6084
        # TODO(htuch): Remove when #6084 is fixed
        "//bazel:asan_build": {"ENVOY_CONFIG_ASAN": "1"},
        "//bazel:msan_build": {"ENVOY_CONFIG_MSAN": "1"},
        "//conditions:default": {},
    }),
    lib_source = "@com_github_moonjit_moonjit//:all",
    out_include_dir = "include/moonjit-2.2",
    out_static_libs = ["libluajit-5.1.a"],
    tags = ["skip_on_windows"],
)

configure_make(
    name = "colm",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "AUTOMAKE=automake",
        "ACLOCAL=aclocal",
        "--disable-shared",
        "--enable-static",
    ],
    # Workaround for the issue with statically linked libstdc++
    # using -l:libstdc++.a.
    env = {
        "CXXFLAGS": "-lstdc++ -Wno-unused-command-line-argument",
    },
    lib_source = "@net_colm_open_source_colm//:all",
    out_binaries = ["colm"],
    tags = ["skip_on_windows"],
)

configure_make(
    name = "ragel",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "AUTOMAKE=automake",
        "ACLOCAL=aclocal",
        "--disable-manual",
        "--disable-shared",
        "--enable-static",
        "--with-colm=$EXT_BUILD_DEPS/colm",
    ],
    # Workaround for the issue with statically linked libstdc++
    # using -l:libstdc++.a.
    env = {
        "CXXFLAGS": "-lstdc++ -Wno-unused-command-line-argument",
    },
    lib_source = "@net_colm_open_source_ragel//:all",
    out_binaries = ["ragel"],
    tags = ["skip_on_windows"],
    deps = [":colm"],
)

# ICU used by the language detection filter (i18n).
# Generates a minimal configuration and reduces the size of the ICU locale data filter file.
# https://unicode-org.github.io/icu/userguide/icu_data/buildtool.html
configure_make(
    name = "unicode_icu_build",
    build_data = ["//bazel/foreign_cc:icu_data_filter.json"],
    configure_command = "icu4c/source/configure",
    configure_options = [
        "--enable-option-checking",
        "--enable-static",
        "--enable-tools",
        "--disable-draft",
        "--disable-dyload",
        "--disable-extras",
        "--disable-plugins",
        "--disable-samples",
        "--disable-shared",
        "--disable-tests",
        "--with-data-packaging=static",
    ],
    data = ["@com_github_unicode_org_icu//:all"],
    env = {
        "CXXFLAGS": "-fPIC",
        "CFLAGS": "-fPIC",
        "ICU_DATA_FILTER_FILE": "$(execpath //bazel/foreign_cc:icu_data_filter.json)",
    },
    lib_source = "@com_github_unicode_org_icu//:all",
    out_static_libs = [
        "libicuuc.a",
        "libicudata.a",
    ],
    tags = ["skip_on_windows"],
    alwayslink = True,
)

cc_library(
    name = "unicode_icu",
    defines = [
        "UCONFIG_ONLY_HTML_CONVERSION=1",
        "U_CHARSET_IS_UTF8=1",
        "U_USING_ICU_NAMESPACE=0",
        "UNISTR_FROM_STRING_EXPLICIT=",
        "UNISTR_FROM_CHAR_EXPLICIT=",
    ] + select({
        "//bazel:windows_x86_64": [
            "U_STATIC_IMPLEMENTATION",
            "UNICODE",
            "_UNICODE",
        ],
        "//conditions:default": [],
    }),
    tags = ["skip_on_windows"],
    # Can not be used for the core dataplane due to security concerns
    visibility = ["//contrib/language/filters/http/source:__pkg__"],
    deps = ["unicode_icu_build"],
)

envoy_cmake(
    name = "libsxg",
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "SXG_BUILD_EXECUTABLES": "off",
        "SXG_BUILD_SHARED": "off",
        "SXG_BUILD_STATIC": "on",
        "SXG_WITH_CERT_CHAIN": "off",
        "RUN_TEST": "off",
        "CMAKE_INSTALL_LIBDIR": "lib",
    },
    lib_source = "@com_github_google_libsxg//:all",
    out_static_libs = ["libsxg.a"],
    tags = ["skip_on_windows"],
    deps = ["@boringssl//:ssl"],
)

envoy_cmake(
    name = "ares",
    cache_entries = {
        "CARES_BUILD_TOOLS": "no",
        "CARES_SHARED": "no",
        "CARES_STATIC": "on",
        "CMAKE_CXX_COMPILER_FORCED": "on",
        "CMAKE_INSTALL_LIBDIR": "lib",
    },
    defines = ["CARES_STATICLIB"],
    lib_source = "@com_github_c_ares_c_ares//:all",
    linkopts = select({
        "//bazel:apple": ["-lresolv"],
        "//conditions:default": [],
    }),
    out_static_libs = select({
        "//bazel:windows_x86_64": ["cares.lib"],
        "//conditions:default": ["libcares.a"],
    }),
    postfix_script = select({
        "//bazel:windows_x86_64": "cp -L $EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/src/lib/ares_nameser.h $INSTALLDIR/include/ares_nameser.h && cp -L $EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/include/ares_dns.h $INSTALLDIR/include/ares_dns.h",
        "//conditions:default": "rm -f $INSTALLDIR/include/ares_dns.h && cp -L $EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/include/ares_dns.h $INSTALLDIR/include/ares_dns.h",
    }),
)

envoy_cmake(
    name = "curl",
    cache_entries = {
        "BUILD_CURL_EXE": "off",
        "BUILD_TESTING": "off",
        "BUILD_SHARED_LIBS": "off",
        "CURL_HIDDEN_SYMBOLS": "off",
        "CURL_USE_LIBSSH2": "off",
        "CURL_USE_LIBPSL": "off",
        "CURL_BROTLI": "off",
        "CURL_USE_GSSAPI": "off",
        "HTTP_ONLY": "on",
        "CMAKE_INSTALL_LIBDIR": "lib",
        # Explicitly enable Unix sockets, once afunix.h is correctly detected
        # "USE_UNIX_SOCKETS": "on",
        # Explicitly disable "Windows" crypto for Windows
        "CURL_DISABLE_CRYPTO_AUTH": "on",
        # C-Ares.
        "ENABLE_ARES": "on",
        "CARES_LIBRARY": "$EXT_BUILD_DEPS/ares",
        "CARES_INCLUDE_DIR": "$EXT_BUILD_DEPS/ares/include",
        # SSL (via Envoy's SSL dependency) is disabled, curl's CMake uses
        # FindOpenSSL.cmake which fails at what looks like version parsing
        # (the libraries are found ok).
        "CURL_CA_PATH": "none",
        "CURL_USE_OPENSSL": "off",
        "OPENSSL_ROOT_DIR": "$EXT_BUILD_DEPS",
        # Avoid libidn2
        "USE_LIBIDN2": "off",
        # NGHTTP2.
        "USE_NGHTTP2": "on",
        "NGHTTP2_LIBRARY": "$EXT_BUILD_DEPS/nghttp2",
        "NGHTTP2_INCLUDE_DIR": "$EXT_BUILD_DEPS/nghttp2/include",
        # ZLIB.
        "CURL_ZLIB": "on",
        "ZLIB_LIBRARY": "$EXT_BUILD_DEPS/zlib",
        "ZLIB_INCLUDE_DIR": "$EXT_BUILD_DEPS/zlib/include",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    defines = ["CURL_STATICLIB"],
    generate_crosstool_file = True,
    lib_source = "@com_github_curl//:all",
    out_static_libs = select({
        "//bazel:windows_x86_64": ["libcurl.lib"],
        "//conditions:default": ["libcurl.a"],
    }),
    deps = [
        ":ares",
        ":nghttp2",
        "//external:ssl",
        "//external:zlib",
    ],
)

envoy_cmake(
    name = "event",
    cache_entries = {
        "EVENT__DISABLE_OPENSSL": "on",
        "EVENT__DISABLE_MBEDTLS": "on",
        "EVENT__DISABLE_REGRESS": "on",
        "EVENT__DISABLE_TESTS": "on",
        "EVENT__LIBRARY_TYPE": "STATIC",
        # Force _GNU_SOURCE on for Android builds. This would be contained in
        # a 'select' but the downstream macro uses a select on all of these
        # options, and they cannot be nested.
        # If https://github.com/bazelbuild/rules_foreign_cc/issues/289 is fixed
        # this can be removed.
        # More details https://github.com/envoyproxy/envoy-mobile/issues/116
        "_GNU_SOURCE": "on",
    },
    lib_source = "@com_github_libevent_libevent//:all",
    out_static_libs = select({
        # macOS organization of libevent is different from Windows/Linux.
        # Including libevent_core is a requirement on those platforms, but
        # results in duplicate symbols when built on macOS.
        # See https://github.com/envoyproxy/envoy-mobile/issues/677 for details.
        "//bazel:apple": [
            "libevent.a",
            "libevent_pthreads.a",
        ],
        "//bazel:windows_x86_64": [
            "event.lib",
            "event_core.lib",
        ],
        "//conditions:default": [
            "libevent.a",
            "libevent_pthreads.a",
            "libevent_core.a",
        ],
    }),
)

envoy_cmake(
    name = "llvm",
    cache_entries = {
        # Disable both: BUILD and INCLUDE, since some of the INCLUDE
        # targets build code instead of only generating build files.
        "LLVM_BUILD_BENCHMARKS": "off",
        "LLVM_INCLUDE_BENCHMARKS": "off",
        "LLVM_BUILD_DOCS": "off",
        "LLVM_INCLUDE_DOCS": "off",
        "LLVM_BUILD_EXAMPLES": "off",
        "LLVM_INCLUDE_EXAMPLES": "off",
        "LLVM_BUILD_RUNTIME": "off",
        "LLVM_BUILD_RUNTIMES": "off",
        "LLVM_INCLUDE_RUNTIMES": "off",
        "LLVM_BUILD_TESTS": "off",
        "LLVM_INCLUDE_TESTS": "off",
        "LLVM_BUILD_TOOLS": "off",
        "LLVM_INCLUDE_TOOLS": "off",
        "LLVM_BUILD_UTILS": "off",
        "LLVM_INCLUDE_UTILS": "off",
        "LLVM_ENABLE_IDE": "off",
        "LLVM_ENABLE_LIBEDIT": "off",
        "LLVM_ENABLE_LIBXML2": "off",
        "LLVM_ENABLE_TERMINFO": "off",
        "LLVM_ENABLE_ZLIB": "off",
        "LLVM_TARGETS_TO_BUILD": "X86",
        "CMAKE_CXX_COMPILER_FORCED": "on",
        # Workaround for the issue with statically linked libstdc++
        # using -l:libstdc++.a.
        "CMAKE_CXX_FLAGS": "-lstdc++",
    },
    env = {
        # Workaround for the -DDEBUG flag added in fastbuild on macOS,
        # which conflicts with DEBUG macro used in LLVM.
        "CFLAGS": "-UDEBUG",
        "CXXFLAGS": "-UDEBUG",
        "ASMFLAGS": "-UDEBUG",
    },
    lib_source = "@org_llvm_llvm//:all",
    out_static_libs = select({
        "//conditions:default": [
            # This list must be updated when the bazel llvm version is updated
            # (in `bazel/repository_locations.bzl`)
            #
            # The list can be regenerated by compiling the correct/updated llvm version
            # from sources and running:
            #
            #  `llvm-config --libnames`
            #
            "libLLVMWindowsManifest.a",
            "libLLVMXRay.a",
            "libLLVMLibDriver.a",
            "libLLVMDlltoolDriver.a",
            "libLLVMCoverage.a",
            "libLLVMLineEditor.a",
            "libLLVMX86Disassembler.a",
            "libLLVMX86AsmParser.a",
            "libLLVMX86CodeGen.a",
            "libLLVMX86Desc.a",
            "libLLVMX86Info.a",
            "libLLVMOrcJIT.a",
            "libLLVMMCJIT.a",
            "libLLVMJITLink.a",
            "libLLVMOrcTargetProcess.a",
            "libLLVMOrcShared.a",
            "libLLVMInterpreter.a",
            "libLLVMExecutionEngine.a",
            "libLLVMRuntimeDyld.a",
            "libLLVMSymbolize.a",
            "libLLVMDebugInfoPDB.a",
            "libLLVMDebugInfoGSYM.a",
            "libLLVMOption.a",
            "libLLVMObjectYAML.a",
            "libLLVMMCA.a",
            "libLLVMMCDisassembler.a",
            "libLLVMLTO.a",
            "libLLVMPasses.a",
            "libLLVMCFGuard.a",
            "libLLVMCoroutines.a",
            "libLLVMObjCARCOpts.a",
            "libLLVMHelloNew.a",
            "libLLVMipo.a",
            "libLLVMVectorize.a",
            "libLLVMLinker.a",
            "libLLVMInstrumentation.a",
            "libLLVMFrontendOpenMP.a",
            "libLLVMFrontendOpenACC.a",
            "libLLVMExtensions.a",
            "libLLVMDWARFLinker.a",
            "libLLVMGlobalISel.a",
            "libLLVMMIRParser.a",
            "libLLVMAsmPrinter.a",
            "libLLVMDebugInfoDWARF.a",
            "libLLVMSelectionDAG.a",
            "libLLVMCodeGen.a",
            "libLLVMIRReader.a",
            "libLLVMAsmParser.a",
            "libLLVMInterfaceStub.a",
            "libLLVMFileCheck.a",
            "libLLVMFuzzMutate.a",
            "libLLVMTarget.a",
            "libLLVMScalarOpts.a",
            "libLLVMInstCombine.a",
            "libLLVMAggressiveInstCombine.a",
            "libLLVMTransformUtils.a",
            "libLLVMBitWriter.a",
            "libLLVMAnalysis.a",
            "libLLVMProfileData.a",
            "libLLVMObject.a",
            "libLLVMTextAPI.a",
            "libLLVMMCParser.a",
            "libLLVMMC.a",
            "libLLVMDebugInfoCodeView.a",
            "libLLVMDebugInfoMSF.a",
            "libLLVMBitReader.a",
            "libLLVMCore.a",
            "libLLVMRemarks.a",
            "libLLVMBitstreamReader.a",
            "libLLVMBinaryFormat.a",
            "libLLVMSupport.a",
            "libLLVMDemangle.a",
        ],
    }),
    tags = ["skip_on_windows"],
    alwayslink = True,
)

envoy_cmake(
    name = "nghttp2",
    cache_entries = {
        "ENABLE_LIB_ONLY": "on",
        "ENABLE_SHARED_LIB": "off",
        "ENABLE_STATIC_LIB": "on",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    cmake_files_dir = "$BUILD_TMPDIR/lib/CMakeFiles",
    debug_cache_entries = {"ENABLE_DEBUG": "on"},
    defines = ["NGHTTP2_STATICLIB"],
    lib_source = "@com_github_nghttp2_nghttp2//:all",
    out_static_libs = select({
        "//bazel:windows_x86_64": ["nghttp2.lib"],
        "//conditions:default": ["libnghttp2.a"],
    }),
)

envoy_cmake(
    name = "wamr",
    cache_entries = {
        "WAMR_BUILD_INTERP": "1",
        "WAMR_BUILD_FAST_INTERP": "1",
        "WAMR_BUILD_JIT": "0",
        "WAMR_BUILD_LAZY_JIT": "0",
        "WAMR_BUILD_AOT": "0",
        "WAMR_BUILD_SIMD": "0",
        "WAMR_BUILD_MULTI_MODULE": "1",
        "WAMR_BUILD_LIBC_WASI": "0",
        "WAMR_BUILD_TAIL_CALL": "1",
    },
    lib_source = "@com_github_wamr//:all",
    out_static_libs = ["libvmlib.a"],
    tags = ["skip_on_windows"],
)

envoy_cmake(
    name = "wavm",
    cache_entries = {
        "LLVM_DIR": "$EXT_BUILD_DEPS/copy_llvm/llvm/lib/cmake/llvm",
        "WAVM_ENABLE_STATIC_LINKING": "on",
        "WAVM_ENABLE_RELEASE_ASSERTS": "on",
        "WAVM_ENABLE_UNWIND": "on",
        # Workaround for the issue with statically linked libstdc++
        # using -l:libstdc++.a.
        "CMAKE_CXX_FLAGS": "-lstdc++ -Wno-unused-command-line-argument",
    },
    env = {
        # Workaround for the -DDEBUG flag added in fastbuild on macOS,
        # which conflicts with DEBUG macro used in LLVM.
        "CFLAGS": "-UDEBUG",
        "CXXFLAGS": "-UDEBUG -Wno-error=unused-but-set-variable",
        "ASMFLAGS": "-UDEBUG",
    },
    lib_source = "@com_github_wavm_wavm//:all",
    out_binaries = ["wavm"],
    out_static_libs = select({
        "//conditions:default": [
            "libWAVM.a",
            "libWAVMUnwind.a",
        ],
    }),
    tags = ["skip_on_windows"],
    deps = [":llvm"],
)

envoy_cmake(
    name = "zlib",
    cache_entries = {
        "CMAKE_CXX_COMPILER_FORCED": "on",
        "CMAKE_C_COMPILER_FORCED": "on",
        "SKIP_BUILD_EXAMPLES": "on",
        "BUILD_SHARED_LIBS": "off",

        # The following entries are for zlib-ng. Since zlib and zlib-ng are compatible source
        # codes and CMake ignores unknown cache entries, it is fine to combine it into one
        # dictionary.
        #
        # Reference: https://github.com/zlib-ng/zlib-ng#build-options.
        "ZLIB_COMPAT": "on",
        "ZLIB_ENABLE_TESTS": "off",

        # Warning: Turning WITH_OPTIM to "on" doesn't pass ZlibCompressorImplTest.CallingChecksum.
        "WITH_OPTIM": "on",
        # However turning off SSE4 fixes it.
        "WITH_SSE4": "off",

        # Warning: Turning WITH_NEW_STRATEGIES to "on" doesn't pass gzip compressor fuzz test.
        # Turning this off means falling into NO_QUICK_STRATEGY route.
        "WITH_NEW_STRATEGIES": "off",

        # Only allow aligned address.
        # Reference: https://github.com/zlib-ng/zlib-ng#advanced-build-options.
        "UNALIGNED_OK": "off",
    },
    lib_source = select({
        "//bazel:zlib_ng": "@com_github_zlib_ng_zlib_ng//:all",
        "//conditions:default": "@net_zlib//:all",
    }),
    out_static_libs = select({
        "//bazel:windows_x86_64": ["zlib.lib"],
        "//conditions:default": ["libz.a"],
    }),
)

envoy_cmake(
    name = "zstd",
    build_data = ["@com_github_facebook_zstd//:all"],
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "ZSTD_BUILD_SHARED": "off",
        "ZSTD_BUILD_STATIC": "on",
    },
    lib_source = "@com_github_facebook_zstd//:all",
    out_static_libs = select({
        "//bazel:windows_x86_64": ["zstd_static.lib"],
        "//conditions:default": ["libzstd.a"],
    }),
    working_directory = "build/cmake",
)
