.. _install_requirements:

Requirements
============

Envoy was initially developed and deployed on Ubuntu 14 LTS. It should work on any reasonably
recent Linux including Ubuntu 16 LTS.

Envoy has the following requirements:

* GCC 5+ (for C++14 support)
* `backward <https://github.com/bombela/backward-cpp>`_ (last tested with 1.3)
* `Bazel <https://github.com/bazelbuild/bazel>`_ (last tested with 0.5.3)
* `BoringSSL <https://boringssl.googlesource.com/boringssl>`_ (last tested with sha ae9f0616c58bddcbe7a6d80d29d796bee9aaff2e)
* `c-ares <https://github.com/c-ares/c-ares>`_ (last tested with 1.13.0)
* `spdlog <https://github.com/gabime/spdlog>`_ (last tested with 0.14.0)
* `fmtlib <https://github.com/fmtlib/fmt/>`_ (last tested with 4.0.0)
* `gperftools <https://github.com/gperftools/gperftools>`_ (last tested with 2.6.1)
* `http-parser <https://github.com/nodejs/http-parser>`_ (last tested with 2.7.1)
* `libevent <http://libevent.org/>`_ (last tested with 2.1.8)
* `lightstep-tracer-cpp <https://github.com/lightstep/lightstep-tracer-cpp/>`_ (last tested with 0.36)
* `luajit <http://luajit.org/>`_ (last tested with 2.0.5)
* `nghttp2 <https://github.com/nghttp2/nghttp2>`_ (last tested with 1.25.0)
* `protobuf <https://github.com/google/protobuf>`_ (last tested with 3.4.0)
* `tclap <http://tclap.sourceforge.net/>`_ (last tested with 1.2.1)
* `rapidjson <https://github.com/miloyip/rapidjson/>`_ (last tested with 1.1.0)
* `xxHash <https://github.com/Cyan4973/xxHash>`_ (last tested with 0.6.3)
* `yaml-cpp <https://github.com/jbeder/yaml-cpp>`_ (last tested with sha e2818c423e5058a02f46ce2e519a82742a8ccac9)
* `zlib <https://github.com/madler/zlib>`_ (last tested with 1.2.11)

In order to compile and run the tests the following is required:

* `googletest <https://github.com/google/googletest>`_ (last tested with sha 43863938377a9ea1399c0596269e0890b5c5515a)

In order to run code coverage the following is required:

* `gcovr <http://gcovr.com/>`_ (last tested with 3.3)
