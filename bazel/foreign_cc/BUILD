load("@rules_cc//cc:defs.bzl", "cc_library")
load("//bazel:envoy_build_system.bzl", "envoy_cmake_external", "envoy_package")
load("@rules_foreign_cc//tools/build_defs:configure.bzl", "configure_make")

licenses(["notice"])  # Apache 2

envoy_package()

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
    make_commands = ["make install-libLTLIBRARIES install-perftoolsincludeHEADERS"],
    static_libraries = select({
        "//bazel:debug_tcmalloc": ["libtcmalloc_debug.a"],
        "//conditions:default": ["libtcmalloc_and_profiler.a"],
    }),
    tags = ["skip_on_windows"],
)

# Workaround for https://github.com/bazelbuild/rules_foreign_cc/issues/227
cc_library(
    name = "gperftools",
    tags = ["skip_on_windows"],
    deps = [
        "gperftools_build",
    ],
)

configure_make(
    name = "luajit",
    configure_command = "build.py",
    configure_env_vars = select({
        # This shouldn't be needed! See
        # https://github.com/envoyproxy/envoy/issues/6084
        # TODO(htuch): Remove when #6084 is fixed
        "//bazel:asan_build": {"ENVOY_CONFIG_ASAN": "1"},
        "//bazel:msan_build": {"ENVOY_CONFIG_MSAN": "1"},
        "//bazel:windows_dbg_build": {"WINDOWS_DBG_BUILD": "debug"},
        "//conditions:default": {},
    }),
    lib_source = "@com_github_luajit_luajit//:all",
    make_commands = [],
    out_include_dir = "include/luajit-2.1",
    static_libraries = select({
        "//bazel:windows_x86_64": ["lua51.lib"],
        "//conditions:default": ["libluajit-5.1.a"],
    }),
)

configure_make(
    name = "moonjit",
    configure_command = "build.py",
    configure_env_vars = select({
        # This shouldn't be needed! See
        # https://github.com/envoyproxy/envoy/issues/6084
        # TODO(htuch): Remove when #6084 is fixed
        "//bazel:asan_build": {"ENVOY_CONFIG_ASAN": "1"},
        "//bazel:msan_build": {"ENVOY_CONFIG_MSAN": "1"},
        "//conditions:default": {},
    }),
    lib_source = "@com_github_moonjit_moonjit//:all",
    make_commands = [],
    out_include_dir = "include/moonjit-2.2",
    static_libraries = ["libluajit-5.1.a"],
    tags = ["skip_on_windows"],
)

envoy_cmake_external(
    name = "ares",
    cache_entries = {
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
    postfix_script = select({
        "//bazel:windows_x86_64": "cp -L $EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/src/lib/nameser.h $INSTALLDIR/include/nameser.h && cp -L $EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/src/lib/ares_dns.h $INSTALLDIR/include/ares_dns.h",
        "//conditions:default": "cp -L $EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/src/lib/ares_dns.h $INSTALLDIR/include/ares_dns.h",
    }),
    static_libraries = select({
        "//bazel:windows_x86_64": ["cares.lib"],
        "//conditions:default": ["libcares.a"],
    }),
)

envoy_cmake_external(
    name = "curl",
    cache_entries = {
        "BUILD_CURL_EXE": "off",
        "BUILD_TESTING": "off",
        "BUILD_SHARED_LIBS": "off",
        "CURL_HIDDEN_SYMBOLS": "off",
        "CMAKE_USE_LIBSSH2": "off",
        "CURL_BROTLI": "off",
        "CMAKE_USE_GSSAPI": "off",
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
        "CMAKE_USE_OPENSSL": "off",
        "OPENSSL_ROOT_DIR": "$EXT_BUILD_DEPS",
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
    static_libraries = select({
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

envoy_cmake_external(
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
        # More details https://github.com/lyft/envoy-mobile/issues/116
        "_GNU_SOURCE": "on",
    },
    lib_source = "@com_github_libevent_libevent//:all",
    static_libraries = select({
        # macOS organization of libevent is different from Windows/Linux.
        # Including libevent_core is a requirement on those platforms, but
        # results in duplicate symbols when built on macOS.
        # See https://github.com/lyft/envoy-mobile/issues/677 for details.
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

envoy_cmake_external(
    name = "llvm",
    cache_entries = {
        # Disable both: BUILD and INCLUDE, since some of the INCLUDE
        # targets build code instead of only generating build files.
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
    env_vars = {
        # Workaround for the -DDEBUG flag added in fastbuild on macOS,
        # which conflicts with DEBUG macro used in LLVM.
        "CFLAGS": "-UDEBUG",
        "CXXFLAGS": "-UDEBUG",
        "ASMFLAGS": "-UDEBUG",
    },
    lib_source = "@org_llvm_llvm//:all",
    static_libraries = select({
        "//conditions:default": [
            # Order from llvm-config --libnames.
            "libLLVMLTO.a",
            "libLLVMPasses.a",
            "libLLVMObjCARCOpts.a",
            "libLLVMSymbolize.a",
            "libLLVMDebugInfoPDB.a",
            "libLLVMDebugInfoDWARF.a",
            "libLLVMFuzzMutate.a",
            "libLLVMTableGen.a",
            "libLLVMDlltoolDriver.a",
            "libLLVMLineEditor.a",
            "libLLVMOrcJIT.a",
            "libLLVMCoverage.a",
            "libLLVMMIRParser.a",
            "libLLVMObjectYAML.a",
            "libLLVMLibDriver.a",
            "libLLVMOption.a",
            "libLLVMWindowsManifest.a",
            "libLLVMX86Disassembler.a",
            "libLLVMX86AsmParser.a",
            "libLLVMX86CodeGen.a",
            "libLLVMGlobalISel.a",
            "libLLVMSelectionDAG.a",
            "libLLVMAsmPrinter.a",
            "libLLVMDebugInfoCodeView.a",
            "libLLVMDebugInfoMSF.a",
            "libLLVMX86Desc.a",
            "libLLVMMCDisassembler.a",
            "libLLVMX86Info.a",
            "libLLVMX86Utils.a",
            "libLLVMMCJIT.a",
            "libLLVMInterpreter.a",
            "libLLVMExecutionEngine.a",
            "libLLVMRuntimeDyld.a",
            "libLLVMCodeGen.a",
            "libLLVMTarget.a",
            "libLLVMCoroutines.a",
            "libLLVMipo.a",
            "libLLVMInstrumentation.a",
            "libLLVMVectorize.a",
            "libLLVMScalarOpts.a",
            "libLLVMLinker.a",
            "libLLVMIRReader.a",
            "libLLVMAsmParser.a",
            "libLLVMInstCombine.a",
            "libLLVMTransformUtils.a",
            "libLLVMBitWriter.a",
            "libLLVMAnalysis.a",
            "libLLVMProfileData.a",
            "libLLVMObject.a",
            "libLLVMMCParser.a",
            "libLLVMMC.a",
            "libLLVMBitReader.a",
            "libLLVMBitstreamReader.a",
            "libLLVMCore.a",
            "libLLVMBinaryFormat.a",
            "libLLVMSupport.a",
            "libLLVMDemangle.a",
            "libLLVMRemarks.a",
            "libLLVMCFGuard.a",
            "libLLVMTextAPI.a",
        ],
    }),
)

envoy_cmake_external(
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
    static_libraries = select({
        "//bazel:windows_x86_64": ["nghttp2.lib"],
        "//conditions:default": ["libnghttp2.a"],
    }),
)

envoy_cmake_external(
    name = "wavm",
    binaries = ["wavm"],
    cache_entries = {
        "LLVM_DIR": "$EXT_BUILD_DEPS/copy_llvm/llvm/lib/cmake/llvm",
        "WAVM_ENABLE_STATIC_LINKING": "on",
        "WAVM_ENABLE_RELEASE_ASSERTS": "on",
        "WAVM_ENABLE_UNWIND": "no",
        # Workaround for the issue with statically linked libstdc++
        # using -l:libstdc++.a.
        "CMAKE_CXX_FLAGS": "-lstdc++ -Wno-unused-command-line-argument",
    },
    defines = ["ENVOY_WASM_WAVM"],
    env_vars = {
        # Workaround for the -DDEBUG flag added in fastbuild on macOS,
        # which conflicts with DEBUG macro used in LLVM.
        "CFLAGS": "-UDEBUG",
        "CXXFLAGS": "-UDEBUG",
        "ASMFLAGS": "-UDEBUG",
    },
    lib_source = "@com_github_wavm_wavm//:all",
    static_libraries = select({
        "//conditions:default": [
            "libWAVM.a",
        ],
    }),
    deps = [":llvm"],
)

envoy_cmake_external(
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
    static_libraries = select({
        "//bazel:windows_x86_64": ["zlib.lib"],
        "//conditions:default": ["libz.a"],
    }),
)
