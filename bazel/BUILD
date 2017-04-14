package(default_visibility = ["//visibility:public"])

exports_files(["gen_sh_test_runner.sh"])

config_setting(
    name = "disable_tcmalloc",
    values = {"linkopt": "-fsanitize=address"},
)
