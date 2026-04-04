load("@rules_python//python:defs.bzl", "py_binary")
# Example python binary that uses the dynamic python validation code.
#
# Exactly as example_cc above except different label. Example:
#
# bazel run //:example_py -- $(pwd)/valid.textproto

# Example C++ binary that uses the generated validation code.
#
# This binary attempts to read files named on the command line as binary protos.
# To try it out, first use Bazel to build this binary:
#
#   bazel build //:example_cc
#
# Now run it on some files. From the repository root directory:
#
#   bazel run //:example_cc -- $(pwd)/example.cc
#
# The binary will fail because example.cc is not a valid textproto. Luckily
# this directory contains two textprotos already: valid.textproto and
# invalid.textproto. From the root directory:
#
#   bazel run //:example_cc -- $(pwd)/valid.textproto
#
# which succeeds, or
#
#   bazel run //:example_cc -- $(pwd)/invalid.textproto
#
# which fails.
cc_binary(
    name = "example_cc",
    srcs = ["example.cc"],
    deps = ["//foo:bar_cc_proto"],
)

py_binary(
    name = "example_py",
    srcs = ["example.py"],
    main = "example.py",
    srcs_version = "PY3",
    deps = [
        "//foo:bar_py_proto",
        "@com_envoyproxy_protoc_gen_validate//python:validator_py",
    ],
)

# Test that the example textproto inputs evoke the right responses.
[
    sh_test(
        name = "example_{lang}_test_{which}".format(
            lang = lang,
            which = which,
        ),
        srcs = ["example_test.sh"],
        args = [
            "$(location :example_{lang})".format(lang = lang),
            str(code),
            "$(location :{which})".format(which = which),
        ],
        data = [
            which,
            ":example_{lang}".format(lang = lang),
        ],
    )
    for lang in (
        "cc",
        "py",
    )
    for (which, code) in (
        ("valid.textproto", 0),
        ("invalid.textproto", 1),
    )
]
