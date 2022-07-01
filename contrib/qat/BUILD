load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

licenses(["notice"])  # Apache 2

configure_make(
    name = "qatlib",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "--disable-systemd",
        "--with-pic",
        "--enable-static",
        "--enable-shared",
        "--enable-samples=no",
    ],
    lib_source = "@com_github_intel_qatlib//:all",
    out_shared_libs = [
        "libqat.so",
        "libusdm.so",
    ],
    out_static_libs = [
        "libqat.a",
        "libusdm.a",
    ],
    visibility = ["//visibility:public"],
    deps = ["@boringssl//:ssl"],
    alwayslink = True,
)
