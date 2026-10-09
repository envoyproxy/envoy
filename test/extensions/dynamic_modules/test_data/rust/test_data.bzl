load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@rules_rust//rust:defs.bzl", "rust_clippy", "rust_shared_library", "rust_static_library", "rust_test")
load("//source/extensions/dynamic_modules:dynamic_modules.bzl", "envoy_dynamic_module_prefix_symbols")

_WINDOWS_X86_64 = Label("//bazel:windows_x86_64")

# "-undefined dynamic_lookup" is only meaningful for ELF and Mach-O linkers. On Windows the
# SDK imports the Envoy callbacks via raw-dylib instead.
_RUSTC_FLAGS = select({
    _WINDOWS_X86_64: [],
    "//conditions:default": ["-C", "link-args=-Wl,-undefined,dynamic_lookup"],
})

def test_program(name):
    srcs = [name + ".rs"]
    if name + "_test.rs" in native.glob(["*.rs"]):
        srcs = srcs + [name + "_test.rs"]

    _name = "_" + name
    rust_shared_library(
        name = _name,
        srcs = srcs,
        edition = "2021",
        crate_root = name + ".rs",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
        ],
        rustc_flags = _RUSTC_FLAGS,
    )

    _static_name = name + "_static"
    _static_lib_name = name + "_static_lib"

    rust_static_library(
        name = _static_lib_name,
        srcs = srcs,
        edition = "2021",
        crate_root = name + ".rs",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
        ],
        rustc_flags = _RUSTC_FLAGS,
    )

    envoy_dynamic_module_prefix_symbols(
        name = _static_name,
        module_name = _static_name,
        archive = ":" + _static_lib_name,
    )

    rust_clippy(
        name = "clippy_" + name,
        tags = ["nocoverage"],
        deps = [":" + _name],
        testonly = True,
    )

    rust_test(
        name = "test_" + name,
        srcs = srcs,
        crate_root = name + ".rs",
        edition = "2021",
        deps = [
            "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk_mock",
        ],
        tags = [
            # It is a known issue that TSAN detectes a false positive in the test runner of Rust toolchain:
            # https://github.com/rust-lang/rust/issues/39608
            # To avoid this issue, we need to use nightly and pass RUSTFLAGS="-Zsanitizer=thread" to this target only
            # when we run the test with TSAN: https://github.com/rust-lang/rust/commit/4b91729df22015bd412f6fc0fa397785d1e2159c
            # However, that causes symbol conflicts between the sanitizer built by Rust and the one built by Bazel.
            # Moreover, we also need to rebuild the Rust std-lib with the cargo option "-Zbuild-std", but that is
            # not supported by the rules_rust yet: https://github.com/bazelbuild/rules_rust/issues/2068
            # So, we disable TSAN for now. In contrast, ASAN works without any issue.
            "no_tsan",
            "nocoverage",
        ],
    )

    # On Windows, rust_shared_library also outputs the import library (and possibly a PDB),
    # so select the DLL itself.
    _dll_name = _name + "_dll"
    native.genrule(
        name = _dll_name,
        srcs = [":" + _name],
        outs = [_dll_name + ".dll"],
        cmd = "for f in $(SRCS); do case $$f in *.dll) cp $$f $@;; esac; done",
        target_compatible_with = ["@platforms//os:windows"],
    )

    # Copy the shared library to the expected name especially for MacOS which
    # defaults to lib<name>.dylib.
    copy_file(
        name = name,
        src = select({
            _WINDOWS_X86_64: ":" + _dll_name,
            "//conditions:default": ":" + _name,
        }),
        out = "lib{}.so".format(name),
    )
