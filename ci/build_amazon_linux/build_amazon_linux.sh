# Build envoy binary on amazon linux ami.
#
# Much of this comes from:
# https://github.com/lyft/envoy/blob/master/ci/build_container/build_container.sh
# https://github.com/lyft/envoy/blob/master/ci/do_ci.sh
#
# Helpful links:
# https://lyft.github.io/envoy/docs/install/requirements.html
# https://lyft.github.io/envoy/docs/install/building.html
# https://gcc.gnu.org/wiki/InstallingGCC

# prerequisite
# run in screen!
set -x
NUM_CPUS=`grep -c ^processor /proc/cpuinfo`
echo "building using $NUM_CPUS CPUs"
ENVOY_ROOT=/mnt/envoy-root
sudo yum install -y cmake gmp-devel mpfr-devel libmpc-devel libgcc48.i686 glibc-devel.i686 golang clang
sudo mkdir -p $ENVOY_ROOT
sudo chown `whoami`:`whoami` $ENVOY_ROOT
cd $ENVOY_ROOT

# gcc
wget http://www.netgull.com/gcc/releases/gcc-4.9.4/gcc-4.9.4.tar.gz
tar zxf gcc-4.9.4.tar.gz
mkdir -p objdir
cd objdir
$PWD/../gcc-4.9.4/configure --prefix=$PWD/../gcc-4.9.4 --enable-languages=c,c++
make -j $NUM_CPUS
make install
cd ..
export PATH=$PWD/gcc-4.9.4/bin:$PATH
export CC=`which gcc`
export CXX=`which g++`

# Build artifacts
THIRDPARTY_DIR=$PWD/thirdparty
THIRDPARTY_BUILD=$PWD/thirdparty_build
mkdir -p $THIRDPARTY_DIR
mkdir -p $THIRDPARTY_BUILD
cd $THIRDPARTY_DIR

# libevent
wget https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz
tar xf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --disable-libevent-regress --disable-openssl
make install
cd ..

# boring ssl
git clone https://boringssl.googlesource.com/boringssl
cd boringssl
git reset --hard be2ee342d3781ddb954f91f8a7e660c6f59e87e5
cmake .
make
cp -r include/* $THIRDPARTY_BUILD/include
cp ssl/libssl.a $THIRDPARTY_BUILD/lib
cp crypto/libcrypto.a $THIRDPARTY_BUILD/lib
cd ..

# gperftools
wget https://github.com/gperftools/gperftools/releases/download/gperftools-2.5/gperftools-2.5.tar.gz
tar xf gperftools-2.5.tar.gz
cd gperftools-2.5
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-frame-pointers
make install
cd ..

# nghttp2
wget https://github.com/nghttp2/nghttp2/releases/download/v1.14.1/nghttp2-1.14.1.tar.gz
tar xf nghttp2-1.14.1.tar.gz
cd nghttp2-1.14.1
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no --enable-lib-only
make install
cd ..

# protobuf
wget https://github.com/google/protobuf/releases/download/v3.0.0/protobuf-cpp-3.0.0.tar.gz
tar xf protobuf-cpp-3.0.0.tar.gz
cd protobuf-3.0.0
./configure --prefix=$THIRDPARTY_BUILD --enable-shared=no
make install
cd ..

# cotire
wget https://github.com/sakra/cotire/archive/cotire-1.7.8.tar.gz
tar xf cotire-1.7.8.tar.gz

# spdlog
wget https://github.com/gabime/spdlog/archive/v0.11.0.tar.gz
tar xf v0.11.0.tar.gz

# http-parser
wget -O http-parser-v2.7.0.tar.gz https://github.com/nodejs/http-parser/archive/v2.7.0.tar.gz
tar xf http-parser-v2.7.0.tar.gz
cd http-parser-2.7.0
$CC -O2 -c http_parser.c -o http_parser.o
ar rcs libhttp_parser.a http_parser.o
cp libhttp_parser.a $THIRDPARTY_BUILD/lib
cp http_parser.h $THIRDPARTY_BUILD/include
cd ..

# tclap
wget -O tclap-1.2.1.tar.gz https://sourceforge.net/projects/tclap/files/tclap-1.2.1.tar.gz/download
tar xf tclap-1.2.1.tar.gz

# lightstep
wget https://github.com/lightstep/lightstep-tracer-cpp/releases/download/v0_19/lightstep-tracer-cpp-0.19.tar.gz
tar xf lightstep-tracer-cpp-0.19.tar.gz
cd lightstep-tracer-cpp-0.19
./configure --disable-grpc --prefix=$THIRDPARTY_BUILD --enable-shared=no \
  protobuf_CFLAGS="-I$THIRDPARTY_BUILD/include" protobuf_LIBS="-L$THIRDPARTY_BUILD/lib -lprotobuf"
make install
cd ..

# rapidjson
wget -O rapidjson-1.1.0.tar.gz https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz
tar xf rapidjson-1.1.0.tar.gz

# google test
wget -O googletest-1.8.0.tar.gz https://github.com/google/googletest/archive/release-1.8.0.tar.gz
tar xf googletest-1.8.0.tar.gz
cd googletest-release-1.8.0
cmake -DCMAKE_INSTALL_PREFIX:PATH=$THIRDPARTY_BUILD .
make install
cd ..

# gcovr
wget -O gcovr-3.3.tar.gz https://github.com/gcovr/gcovr/archive/3.3.tar.gz
tar xf gcovr-3.3.tar.gz

# envoy
cd ..
export LD_LIBRARY_PATH=$PWD/gcc-4.9.4/lib64:$LD_LIBRARY_PATH
git clone https://github.com/lyft/envoy.git
mkdir -p envoy/build
cd envoy/build

cmake \
-DENVOY_DEBUG:BOOL=OFF \
-DENVOY_STRIP:BOOL=ON \
-DCLANG-FORMAT:FILEPATH=clang-format \
-DCMAKE_CXX_FLAGS=-static-libstdc++ \
-DENVOY_COTIRE_MODULE_DIR:FILEPATH=$THIRDPARTY_DIR/cotire-cotire-1.7.8/CMake \
-DENVOY_GMOCK_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_GPERFTOOLS_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_GTEST_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_HTTP_PARSER_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_LIBEVENT_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_NGHTTP2_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_SPDLOG_INCLUDE_DIR:FILEPATH=$THIRDPARTY_DIR/spdlog-0.11.0/include \
-DENVOY_TCLAP_INCLUDE_DIR:FILEPATH=$THIRDPARTY_DIR/tclap-1.2.1/include \
-DENVOY_OPENSSL_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_LIGHTSTEP_TRACER_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_PROTOBUF_INCLUDE_DIR:FILEPATH=$THIRDPARTY_BUILD/include \
-DENVOY_PROTOBUF_PROTOC:FILEPATH=$THIRDPARTY_BUILD/bin/protoc \
-DENVOY_GCOVR:FILEPATH=$THIRDPARTY_DIR/gcovr-3.3/scripts/gcovr \
-DENVOY_RAPIDJSON_INCLUDE_DIR:FILEPATH=$THIRDPARTY_DIR/rapidjson-1.1.0/include \
-DENVOY_GCOVR_EXTRA_ARGS:STRING="-e test/* -e build/*" \
-DENVOY_EXE_EXTRA_LINKER_FLAGS:STRING=-L$THIRDPARTY_BUILD/lib \
-DENVOY_TEST_EXTRA_LINKER_FLAGS:STRING=-L$THIRDPARTY_BUILD/lib \
..

cmake -L || /bin/true
make check_format
make -j $NUM_CPUS envoy
cd ../..
ls -l envoy/build/source/exe/envoy
