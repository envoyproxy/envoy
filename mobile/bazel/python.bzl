abi_bzl_template = """\
def python_tag():
    return "{python_tag}"

def abi_tag():
    return "{abi_tag}"
"""

def _get_python_bin(rctx):
    python_label = Label("@python3_12_host//:bin/python3")
    python_bin = str(rctx.path(python_label))
    if not python_bin:
        fail("failed to get python bin")
    return python_bin

def _get_python_tag(rctx, python_bin):
    result = rctx.execute([
        python_bin,
        "-c",
        "import platform;" +
        "assert platform.python_implementation() == 'CPython';" +
        "version = platform.python_version_tuple();" +
        "print(f'cp{version[0]}{version[1]}')",
    ])
    if result.return_code != 0:
        fail("Failed to get python tag: " + result.stderr)
    return result.stdout.splitlines()[0]

def _get_abi_tag(rctx, python_bin):
    result = rctx.execute([
        python_bin,
        "-c",
        "import platform;" +
        "import sys;" +
        "assert platform.python_implementation() == 'CPython';" +
        "version = platform.python_version_tuple();" +
        "print(f'cp{version[0]}{version[1]}{sys.abiflags}')",
    ])
    if result.return_code != 0:
        fail("Failed to get abi tag: " + result.stderr)
    return result.stdout.splitlines()[0]

def _declare_python_abi_impl(rctx):
    python_bin = _get_python_bin(rctx)
    python_tag = _get_python_tag(rctx, python_bin)
    abi_tag = _get_abi_tag(rctx, python_bin)
    rctx.file("BUILD")
    rctx.file(
        "abi.bzl",
        abi_bzl_template.format(
            python_tag = python_tag,
            abi_tag = abi_tag,
        ),
    )

declare_python_abi = repository_rule(
    implementation = _declare_python_abi_impl,
    attrs = {
        "python_version": attr.string(mandatory = True),
    },
    local = True,
)
