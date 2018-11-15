#!/bin/bash

# Please ensure edits to this file don't break the python script 'get_repository_info.py'
# ie. from root of the project run
#
# python bazel/print_dependencies.py
#
# and the bazel run rule
#
# bazel run //bazel:print_dependencies
#
# Since the script use's python's os.path.expandvars we do not quote variables
# in this file.

# use commit where cmake 3.6 feature removed. Unblocks Ubuntu 16.xx or below builds
# TODO (moderation) change back to tarball method on next benchmark release
BENCHMARK_GIT_SHA=505be96ab23056580a3a2315abba048f4428b04e
BENCHMARK_FILE_URL=https://github.com/google/benchmark/archive/$BENCHMARK_GIT_SHA.tar.gz
BENCHMARK_FILE_SHA256=0de43b6eaddd356f1d6cd164f73f37faf2f6c96fd684e1f7ea543ce49c1d144e

CARES_VERSION=1.15.0
CARES_TAG=cares-1_15_0
CARES_FILE_URL=https://github.com/c-ares/c-ares/releases/download/$CARES_TAG/c-ares-$CARES_VERSION.tar.gz
CARES_FILE_SHA256=6cdb97871f2930530c97deb7cf5c8fa4be5a0b02c7cea6e7c7667672a39d6852

GPERFTOOLS_VERSION=2.7
GPERFTOOLS_TAG=gperftools-$GPERFTOOLS_VERSION
GPERFTOOLS_FILE_URL=https://github.com/gperftools/gperftools/releases/download/$GPERFTOOLS_TAG/$GPERFTOOLS_TAG.tar.gz
GPERFTOOLS_FILE_SHA256=1ee8c8699a0eff6b6a203e59b43330536b22bbcbe6448f54c7091e5efb0763c9

# Maintainer provided source tarball does not contain cmake content so using Github tarball.
LIBEVENT_VERSION=2.1.8-stable
LIBEVENT_TAG=release-$LIBEVENT_VERSION
LIBEVENT_FILE_URL=https://github.com/libevent/libevent/archive/$LIBEVENT_TAG.tar.gz
LIBEVENT_FILE_SHA256=316ddb401745ac5d222d7c529ef1eada12f58f6376a66c1118eee803cb70f83d

LUAJIT_VERSION=2.0.5
LUAJIT_FILE_URL=https://github.com/LuaJIT/LuaJIT/archive/v$LUAJIT_VERSION.tar.gz
LUAJIT_FILE_SHA256=8bb29d84f06eb23c7ea4aa4794dbb248ede9fcb23b6989cbef81dc79352afc97

NGHTTP2_VERSION=1.34.0
NGHTTP2_FILE_URL=https://github.com/nghttp2/nghttp2/releases/download/v$NGHTTP2_VERSION/nghttp2-$NGHTTP2_VERSION.tar.gz
NGHTTP2_FILE_SHA256=8889399ddd38aa0405f6e84f1c050a292286089441686b8a9c5e937de4f5b61d

# Pin to this commit to pick up fix for building on Visual Studio 15.8
YAMLCPP_GIT_SHA=0f9a586ca1dc29c2ecb8dd715a315b93e3f40f79
YAMLCPP_FILE_URL=https://github.com/jbeder/yaml-cpp/archive/$YAMLCPP_GIT_SHA.tar.gz
YAMLCPP_FILE_SHA256=53dcffd55f3433b379fcc694f45c54898711c0e29159a7bd02e82a3e0253bac3

ZLIB_VERSION=1.2.11
ZLIB_FILE_URL=https://github.com/madler/zlib/archive/v$ZLIB_VERSION.tar.gz
ZLIB_FILE_SHA256=629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff
