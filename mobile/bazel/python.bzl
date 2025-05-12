abi_bzl_template = """\
def python_tag():
    return "{python_tag}"

def abi_tag():
    return "{abi_tag}"
"""

# we reuse the PYTHON_BIN_PATH environment variable from pybind11 so that the
# ABI tag we detect is always compatible with the version of python that was
# used for the build
_PYTHON_BIN_PATH_ENV = "PYTHON_BIN_PATH"

def _get_python_bin(rctx):
    python_version = rctx.attr.python_version
    if python_version != "2" and python_version != "3":
        fail("python_version must be one of: '2', '3'")

    python_bin = rctx.os.environ.get(_PYTHON_BIN_PATH_ENV)
    if python_bin != None:
        return python_bin

    python_bin = rctx.which("python" + python_version)
    if python_bin != None:
        return python_bin

    fail("cannot find python binary")

def _get_python_tag(rctx, python_bin):
    result = rctx.execute([
        python_bin,
        "-c",
        "import platform;" +
        "assert platform.python_implementation() == 'CPython';" +
        "version = platform.python_version_tuple();" +
        "print(f'cp{version[0]}{version[1]}')",
    ])
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
