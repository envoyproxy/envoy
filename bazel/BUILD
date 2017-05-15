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

# TODO(tschroed): This allows linking against Google code, but is very hacky.
# https://github.com/lyft/envoy/issues/967 to do it properly, at which point
# this can be deleted.
config_setting(
    name = "disable_lightstep_tracing",
    values = {"define": "lightstep_tracing=disabled"},
)
