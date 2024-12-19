load("//test/extensions/dynamic_modules/test_data/rust:test_data.bzl", "test_program")

licenses(["notice"])  # Apache 2

package(default_visibility = [
    "//test/extensions/dynamic_modules:__pkg__",
    "//test/extensions/dynamic_modules/http:__pkg__",
])

test_program(name = "no_op")

test_program(name = "no_program_init")

test_program(name = "program_init_fail")

test_program(name = "abi_version_mismatch")

test_program(name = "http")
