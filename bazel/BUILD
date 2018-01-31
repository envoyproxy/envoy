licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

exports_files([
    "gen_sh_test_runner.sh",
    "sh_test_wrapper.sh",
])

config_setting(
    name = "opt_build",
    values = {"compilation_mode": "opt"},
)

config_setting(
    name = "fastbuild_build",
    values = {"compilation_mode": "fastbuild"},
)

config_setting(
    name = "dbg_build",
    values = {"compilation_mode": "dbg"},
)

config_setting(
    name = "disable_tcmalloc",
    values = {"define": "tcmalloc=disabled"},
)

config_setting(
    name = "disable_signal_trace",
    values = {"define": "signal_trace=disabled"},
)

config_setting(
    name = "disable_hot_restart",
    values = {"define": "hot_restart=disabled"},
)

config_setting(
    name = "disable_google_grpc",
    values = {"define": "google_grpc=disabled"},
)

cc_proto_library(
    name = "grpc_health_proto",
    deps = ["@com_github_grpc_grpc//src/proto/grpc/health/v1:_health_proto_only"],
)

config_setting(
    name = "enable_exported_symbols",
    values = {"define": "exported_symbols=enabled"},
)

config_setting(
    name = "force_libcpp",
    values = {"define": "force_libcpp=enabled"},
)
