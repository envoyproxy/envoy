load("@bazel_skylib//lib:selects.bzl", "selects")

# Set up all the paths, flags, and deps, and then call the boringssl build
BUILD_COMMAND = """
set -eo pipefail

# c++
export CC=$(CC)
# bazel doesnt expose CXX so we have to construct it (or use foreign_cc)
if [[ "%s" == "libc++" ]]; then
    export CXXFLAGS="-stdlib=libc++"
    export LDFLAGS="-stdlib=libc++ -lc++ -lc++abi -lm -pthread"
else
    export CXXFLAGS=""
    export LDFLAGS="-lstdc++ -lm -pthread"
fi

# ninja
NINJA_BINDIR=$$(realpath $$(dirname $(location :ninja_bin)))
export PATH="$${NINJA_BINDIR}:$${PATH}"

# cmake
CMAKE_BINDIR=$$(realpath $$(dirname $(location %s//:bin/cmake)))
export PATH="$${CMAKE_BINDIR}:$$PATH"

# go
GO_BINDIR=$$(realpath $$(dirname $(location %s//:bin/go)))
export GOROOT=$$(dirname "$${GO_BINDIR}")
export GOPATH="$${GOROOT}/gopath"
mkdir -p "$$GOPATH"
export PATH="$${GOPATH}/bin:$${GO_BINDIR}:$${PATH}"

# boringssl
BSSL_SRC=$$(realpath $$(dirname $$(dirname $(location crypto_marker))))
export BSSL_SRC

# We might need to make this configurable if it causes issues outside of CI
export NINJA_CORES=$$(nproc)

CRYPTO_OUT="$$(realpath $(location crypto/libcrypto.a))"
SSL_OUT="$$(realpath $(location ssl/libssl.a))"
export CRYPTO_OUT
export SSL_OUT

OUTPUT=$$(mktemp)
if ! $(location @envoy//bazel/external:boringssl_fips.genrule_cmd) > $$OUTPUT 2>&1; then
    echo "Build failed:"
    cat $$OUTPUT >&2
    exit 1
fi
"""

NINJA_BUILD_COMMAND = """
set -eo pipefail

SRC_DIR=$$(dirname $(location @fips_ninja//:configure.py))
OUT_FILE=$$(realpath $@)
PYTHON_BIN=$$(realpath $(PYTHON3))
export CC=$(CC)
export CXX=$(CC)
# bazel doesnt expose CXX so we have to construct it (or use foreign_cc)
if [[ "%s" == "libc++" ]]; then
    export CXXFLAGS="-stdlib=libc++"
    export LDFLAGS="-stdlib=libc++ -lc++ -lc++abi -lm -pthread"
else
    export CXXFLAGS=""
    export LDFLAGS="-lstdc++ -lm -pthread"
fi
cd $$SRC_DIR
OUTPUT=$$(mktemp)
if ! $${PYTHON_BIN} ./configure.py --bootstrap --with-python=$${PYTHON_BIN} > $$OUTPUT 2>&1; then
    echo "Build failed:" >&2
    cat $$OUTPUT >&2
    exit 1
fi
cp ninja $$OUT_FILE
"""

def _create_boringssl_fips_build_config(lib, arch, arch_alias):
    """Create the config_setting_group combination."""
    conditions = ["@platforms//cpu:%s" % arch]
    if lib == "libc++":
        conditions += ["@envoy//bazel:libc++_enabled"]
    selects.config_setting_group(
        name = "%s_%s" % (arch, lib),
        match_all = conditions,
    )

def _create_boringssl_fips_build_command(lib, arch, arch_alias):
    """Create the command."""
    _create_boringssl_fips_build_config(lib, arch, arch_alias)
    return BUILD_COMMAND % (
        lib,
        "@fips_cmake_linux_%s" % arch,
        "@fips_go_linux_%s" % arch_alias,
    )

def boringssl_fips_build_command(arches, libs):
    """Create conditional commands from the cartesian product of possible arches/stdlib."""
    return {
        ":%s_%s" % (arch, lib): _create_boringssl_fips_build_command(
            lib,
            arch,
            arch_alias,
        )
        for arch, arch_alias in arches.items()
        for lib in libs
    }

def ninja_build_command():
    """Create the ninja command conditioned to correct stdlib."""
    return {
        "@envoy//bazel:libc++_enabled": NINJA_BUILD_COMMAND % "libc++",
        "//conditions:default": NINJA_BUILD_COMMAND % "libstdc++",
    }
