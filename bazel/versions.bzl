# This file needs to be loadable in both Skylark (Bazel Python subset) for the developer-local setup
# and regular bash for the CI Docker image build. As a result, only common syntax may be used.

CARES_REMOTE="https://github.com/c-ares/c-ares.git"
CARES_COMMIT="cares-1_12_0"

BORINGSSL_REMOTE="https://boringssl.googlesource.com/boringssl"
# 2017-02-02
BORINGSSL_COMMIT="bfd36df3da38dbf8828e712f42fbab2a0034bc40"

GCOVR_HTTP_ARCHIVE="https://github.com/gcovr/gcovr/archive/3.3.tar.gz"
GCOVR_PREFIX="gcovr-3.3"

GOOGLETEST_REMOTE="https://github.com/google/googletest.git"
GOOGLETEST_COMMIT="release-1.8.0"

GPERFTOOLS_HTTP_ARCHIVE="https://github.com/gperftools/gperftools/releases/download/gperftools-2.5/gperftools-2.5.tar.gz"
GPERFTOOLS_PREFIX="gperftools-2.5"

HTTP_PARSER_REMOTE="https://github.com/nodejs/http-parser.git"
HTTP_PARSER_COMMIT="v2.7.0"

LIBEVENT_HTTP_ARCHIVE="https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz"
LIBEVENT_PREFIX="libevent-2.1.8-stable"

LIGHTSTEP_HTTP_ARCHIVE="https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_36/lightstep-tracer-cpp-0.36.tar.gz"
LIGHTSTEP_PREFIX="lightstep-tracer-cpp-0.36"

NGHTTP2_HTTP_ARCHIVE="https://github.com/nghttp2/nghttp2/releases/download/v1.20.0/nghttp2-1.20.0.tar.gz"
NGHTTP2_PREFIX="nghttp2-1.20.0"

# Using a non-canonical repository/branch here. This is a workaround to the lack of merge on
# https://github.com/google/protobuf/pull/2508, which is needed for supporting arbitrary CC compiler
# locations from the environment. The branch is
# https://github.com/htuch/protobuf/tree/v3.2.0-default-shell-env, which is the 3.2.0 release with
# the above mentioned PR cherry picked.
PROTOBUF_REMOTE="https://github.com/htuch/protobuf.git"
PROTOBUF_COMMIT="d490587268931da78c942a6372ef57bb53db80da"

RAPIDJSON_REMOTE="https://github.com/miloyip/rapidjson.git"
RAPIDJSON_COMMIT="v1.1.0"

SPDLOG_REMOTE="https://github.com/gabime/spdlog.git"
SPDLOG_COMMIT="v0.11.0"

TCLAP_HTTP_ARCHIVE="https://storage.googleapis.com/istio-build-deps/tclap-1.2.1.tar.gz"
TCLAP_PREFIX="tclap-1.2.1"
