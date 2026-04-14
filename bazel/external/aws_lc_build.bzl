AWS_LC_BUILD_CMD = """
set -eo pipefail
export CLANG_BIN_DIR="$$(cd $$(dirname $(location %s)) && pwd)"
export PATH="$${CLANG_BIN_DIR}:$$PATH"
export NINJA_BIN_DIR="$$(cd $$(dirname $(location :ninja_bin)) && pwd)"
export PATH="$${NINJA_BIN_DIR}:$$PATH"
GO_BINDIR=$$(realpath $$(dirname $(location %s//:bin/go)))
export GOROOT=$$(dirname "$${GO_BINDIR}")
export GOPATH="$${GOROOT}/gopath"
mkdir -p "$$GOPATH"
export PATH="$${GOPATH}/bin:$${GO_BINDIR}:$${PATH}"
CMAKE_BIN_DIR=$$(realpath $$(dirname $(location %s)))
export PATH="$${CMAKE_BIN_DIR}:$$PATH"
$(location @envoy//bazel/external:aws_lc.genrule_cmd) $(location crypto/libcrypto.a) $(location ssl/libssl.a)
"""

SUPPORTED_ARCHES = ["x86_64", "aarch64", "ppc64le"]

def aws_lc_build_command():
    result = {}
    for arch in SUPPORTED_ARCHES:
        if arch == "ppc64le":
            cmake_label = ":cmake_bin"
            clang_label = "@fips_clang_%s//:bin/clang" % arch
            go_label = "@fips_go_%s" % arch
        else:
            cmake_label = "@fips_cmake_linux_%s//:bin/cmake" % arch
            clang_label = "@llvm_toolchain_llvm//:bin/clang"
            go_label = "@fips_go_linux_%s" % ("amd64" if arch == "x86_64" else "arm64")
        result["@platforms//cpu:%s" % arch] = AWS_LC_BUILD_CMD % (
            clang_label,
            go_label,
            cmake_label,
        )
    result["//conditions:default"] = "echo 'Unsupported arch for AWS-LC' && exit 1"
    return result
