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
    lib_source = "@com_github_intel_qatlib//:all",
    out_static_libs = [
        "libqat.a",
        "libusdm.a",
    ],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    visibility = ["//visibility:public"],
    # Use boringssl alias to select fips vs non-fips version.
    deps = ["//bazel:boringcrypto"],
    alwayslink = True,
)
