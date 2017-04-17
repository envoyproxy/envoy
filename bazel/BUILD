package(default_visibility = ["//visibility:public"])

exports_files(["gen_sh_test_runner.sh"])

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
    name = "debug_symbols",
    values = {"define": "debug_symbols=yes"},
)
