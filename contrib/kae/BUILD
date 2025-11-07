load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")
load("//bazel:envoy_build_system.bzl", "envoy_contrib_package")
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_aarch64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Have uadk (KAE driver) outside of any extensions because it may be used by both
# KAE compression library extension and KAE private key provider
# extension.

configure_make(
    name = "uadklib",
    autogen = True,
    configure_in_place = True,
    configure_command = "conf.sh",
    lib_source = "@com_github_huawei_uadk//:all",
    out_shared_libs = [
        "libwd.so",
        "libwd_crypto.so",
    ],
    target_compatible_with = envoy_contrib_linux_aarch64_constraints(),
    visibility = ["//visibility:public"],
    # Use boringssl alias to select fips vs non-fips version.
    deps = ["//bazel:boringssl"],
    alwayslink = True,
)