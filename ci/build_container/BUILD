package(default_visibility = ["//visibility:public"])

exports_files(["build_and_install_deps.sh"])

filegroup(
    name = "prebuilt_build_inputs",
    # FIXME: fill in others
    srcs = ["Makefile"] + glob(["build_recipes/*.sh"]),
)
