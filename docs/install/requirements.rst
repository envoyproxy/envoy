.. _install_requirements:

Requirements
============

Envoy was initially developed and deployed on Ubuntu 14 LTS. It should work on any reasonably
recent Linux including Ubuntu 16 LTS.

Envoy has the following requirements:

* GCC 4.9+ (for C++11 regex support)
* `cotire <https://github.com/sakra/cotire>`_ (last tested with 1.7.8)
* `spdlog <https://github.com/gabime/spdlog>`_ (last tested with 0.11.0)
* `http-parser <https://github.com/nodejs/http-parser>`_ (last tested with 2.7.0)
* `nghttp2 <https://github.com/nghttp2/nghttp2>`_ (last tested with 1.14.1)
* `libevent <http://libevent.org/>`_ (last tested with 2.0.22)
* `tclap <http://tclap.sourceforge.net/>`_ (last tested with 1.2.1)
* `gperftools <https://github.com/gperftools/gperftools>`_ (last tested with 2.5.0)
* `boringSSL <https://boringssl.googlesource.com/borringssl>`_ (last tested with sha 78684e5b222645828ca302e56b40b9daff2b2d27).
  Envoy is built against BoringSSL but `openssl <https://www.openssl.org>`_ should still work.
* `protobuf <https://github.com/google/protobuf>`_ (last tested with 3.0.0)
* `lightstep-tracer-cpp <https://github.com/lightstep/lightstep-tracer-cpp/>`_ (last tested with 0.19)
* `rapidjson <https://github.com/miloyip/rapidjson/>`_ (last tested with 1.1.0)
* `c-ares <https://github.com/c-ares/c-ares>`_ (last tested with 1.12.0)

In order to compile and run the tests the following is required:

* `googletest <https://github.com/google/googletest>`_ (last tested with 1.8.0)

In order to run code coverage the following is required:

* `gcovr <http://gcovr.com/>`_ (last tested with 3.3)
