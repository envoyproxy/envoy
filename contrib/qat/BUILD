load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")
load("//bazel:envoy_build_system.bzl", "envoy_contrib_package")
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Have qatlib outside of any extensions because it may be used by both
# QAT compression library extension and QAT private key provider
# extension.

configure_make(
    name = "qatlib",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "--disable-fast-crc-in-assembler",
        "--disable-systemd",
        "--with-pic",
        "--disable-shared",
        "--enable-static",
        "--enable-samples=no",
    ],
    # Include the numa_archive genrule output so that libnuma.a is available
    # for the linker to find with -lnuma.
    data = ["//bazel/external:numa_archive"],
    # Point to the directory containing the libnuma.a archive created by the
    # numa_archive genrule. The cc_library doesn't produce a file that can be
    # found with -lnuma, so we need to use the genrule output.
    env = {
        "LDFLAGS": " ".join([
            "-L$$EXT_BUILD_ROOT/$(BINDIR)/bazel/external/lib",
            "-L$$EXT_BUILD_ROOT/$(BINDIR)/external/envoy/bazel/external/lib",
        ]),
    },
    lib_source = "@qatlib//:all",
    out_static_libs = [
        "libqat.a",
        "libusdm.a",
    ],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    visibility = ["//visibility:public"],
    # Use crypto label_flag to select the SSL library.
    deps = [
        "//bazel:crypto",
        "@numactl//:numa",
    ],
    alwayslink = True,
)
