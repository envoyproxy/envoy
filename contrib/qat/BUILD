load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

configure_make(
    name = "qatlib",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "--disable-systemd",
        "--disable-shared",
        "--with-pic",
        "--enable-static",
        "--enable-samples=no",
    ],
    lib_source = "@com_github_intel_qatlib//:all",
    out_static_libs = [
        "libqat.a",
        "libusdm.a",
    ],
    visibility = ["//visibility:public"],
    deps = ["@boringssl//:ssl"],
    alwayslink = True,
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
)
