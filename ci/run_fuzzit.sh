#!/bin/bash -eux
# Copyright 2019 fuzzit.dev Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################

function abspath {
    if [[ -d "$1" ]]
    then
        pushd "$1" >/dev/null
        pwd
        popd >/dev/null
    elif [[ -e $1 ]]
    then
        pushd "$(dirname "$1")" >/dev/null
        echo "$(pwd)/$(basename "$1")"
        popd >/dev/null
    else
        echo "$1" does not exist! >&2
        return 127
    fi
}

# Uploading to Fuzzit
export FUZZIT_ARGS="--type $1"
wget -O fuzzit https://github.com/fuzzitdev/fuzzit/releases/download/v2.4.30/fuzzit_Linux_x86_64
chmod a+x fuzzit

PREFIX="`abspath /build/tmp/_bazel_bazel/*/execroot/envoy/bazel-out/k8-fastbuild/bin/test`"

./fuzzit create job ${FUZZIT_ARGS} envoyproxy/access-log-formatter                      $PREFIX/common/access_log/access_log_formatter_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/buffer                                    $PREFIX/common/buffer/buffer_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/new-buffer                                $PREFIX/common/buffer/new_buffer_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/base64                                    $PREFIX/common/common/base64_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/hash                                      $PREFIX/common/common/hash_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/common-utility                            $PREFIX/common/common/utility_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/http-codec-impl                           $PREFIX/common/http/codec_impl_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/http-conn-manager-impl                    $PREFIX/common/http/conn_manager_impl_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/http-header-map-impl                      $PREFIX/common/http/header_map_impl_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/http-utility                              $PREFIX/common/http/utility_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/http2-request-header                      $PREFIX/common/http/http2/request_header_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/http2-response-header                     $PREFIX/common/http/http2/response_header_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/protobuf-value-util                       $PREFIX/common/protobuf/value_util_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/router-header-parser                      $PREFIX/common/router/header_parser_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/router-route                              $PREFIX/common/router/route_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/integration-h1-capture-direct-response    $PREFIX/integration/h1_capture_direct_response_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/integration-h1-capture                    $PREFIX/integration/h1_capture_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/server-server                             $PREFIX/server/server_fuzz_test_with_libfuzzer
./fuzzit create job ${FUZZIT_ARGS} envoyproxy/config-validation-config                  $PREFIX/server/config_validation/config_fuzz_test_with_libfuzzer
