# NOTE: These are all of the third party requirements required to build Envoy. We realize this is
#       not the cleanest cmake way of doing things and we welcome patches from cmake experts to
#       make it better.

# https://github.com/sakra/cotire
# Last tested with 1.7.8
set(ENVOY_COTIRE_MODULE_DIR "/usr/local/google/home/fengli/fengli79/cotire/CMake" CACHE FILEPATH "location of cotire cmake module")

# https://github.com/gabime/spdlog
# Last tested with 0.11.0
set(ENVOY_SPDLOG_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/spdlog/include" CACHE FILEPATH "location of spdlog includes")

# https://github.com/nodejs/http-parser
# Last tested with 2.7.0
set(ENVOY_HTTP_PARSER_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/http-parser" CACHE FILEPATH "location of http-parser includes")

# https://github.com/nghttp2/nghttp2
# Last tested with 1.20.0
set(ENVOY_NGHTTP2_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/nghttp2/lib/includes" CACHE FILEPATH "location of nghttp2 includes")

# http://libevent.org/
# Last tested with 2.1.8
set(ENVOY_LIBEVENT_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/libevent/include" CACHE FILEPATH "location of libevent includes")

# http://tclap.sourceforge.net/
# Last tested with 1.2.1
set(ENVOY_TCLAP_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/tclap-code/include" CACHE FILEPATH "location of tclap includes")

# https://github.com/gperftools/gperftools
# Last tested with 2.5.0
set(ENVOY_GPERFTOOLS_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/gperftools/src" CACHE FILEPATH "location of gperftools includes")

# https://boringssl.googlesource.com/boringssl/+/chromium-stable
set(ENVOY_OPENSSL_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/boringssl/include" CACHE FILEPATH "location of openssl includes")

# https://github.com/c-ares/c-ares
# Last tested with 1.12.0
set(ENVOY_CARES_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/c-ares" CACHE FILEPATH "location of c-ares includes")

# https://github.com/google/protobuf
# Last tested with 3.0.0
set(ENVOY_PROTOBUF_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/protobuf/src" CACHE FILEPATH "location of protobuf includes")
set(ENVOY_PROTOBUF_PROTOC "/usr/local/google/home/fengli/fengli79/protobuf/src/protoc" CACHE FILEPATH "location of protoc")

# http://lightstep.com/
# Last tested with lightstep-tracer-cpp-0.36
set(ENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/lightstep-tracer-cpp/src/c++11" CACHE FILEPATH "location of lighstep tracer includes")

# https://github.com/miloyip/rapidjson
# Last tested with 1.1.0
set(ENVOY_RAPIDJSON_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/rapidjson/include" CACHE FILEPATH "location of rapidjson includes")

# Extra linker flags required to properly link envoy with all of the above libraries.
set(ENVOY_EXE_EXTRA_LINKER_FLAGS "-L/usr/grte/v4/lib64 -L/usr/local/lib -L/usr/local/google/home/fengli/fengli79/gpertools -L/usr/local/google/home/fengli/fengli79/boringssl/build/ssl -L/usr/local/google/home/fengli/fengli79/boringssl/build/crypto -L/usr/local/google/home/fengli/fengli79/nghttp2/lib/.libs -L/usr/local/google/home/fengli/fengli79/lightstep-tracer-cpp/src/c++11/.libs" CACHE STRING "envoy extra linker flags")

#
# Test Requirements
#

# https://github.com/google/googletest
# Last tested with 1.8.0
set(ENVOY_GTEST_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/googletest/googletest/include" CACHE FILEPATH "location of gtest includes")
set(ENVOY_GMOCK_INCLUDE_DIR "/usr/local/google/home/fengli/fengli79/googletest/googlemock/include" CACHE FILEPATH "location of gmock includes")

# http://gcovr.com/
# Last tested with 3.3
set(ENVOY_GCOVR "/usr/local/google/home/fengli/fengli79/gcovr" CACHE FILEPATH "location of gcovr")
set(ENVOY_GCOVR_EXTRA_ARGS "" CACHE STRING "extra arguments to pass to gcovr")

# Extra linker flags required to properly link envoy-test with all of the above libraries.
set(ENVOY_TEST_EXTRA_LINKER_FLAGS "" CACHE STRING "envoy-test extra linker flags")
