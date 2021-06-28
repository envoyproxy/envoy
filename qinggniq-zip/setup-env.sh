#!/bin/bash
set -e

export HTTPS_PROXY=http://gfw.in.zhihu.com:18080
export HTTP_PROXY=http://gfw.in.zhihu.com:18080
export PATH=/usr/lib/llvm-11/bin:/data/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/node/bin:/usr/local/go/bin:/snap/bin

function prepare_install() {
sudo apt-get -y install \
   patch \
   wget \
   libtool \
   cmake \
   automake \
   autoconf \
   make \
   ninja-build \
   curl \
   unzip \
   virtualenv 
}
 
function install_bazel() {
  echo "install bazel"
  if ! command -v bazel &> /dev/null; then
    sudo wget -O /usr/local/bin/bazel https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64
    sudo chmod +x /usr/local/bin/bazel
  fi
}

function download_envoy() {
  echo "download envoy"
  if ! [ -d "./envoy" ]; then
  git clone https://github.com/envoyproxy/envoy.git
  fi
}

function install_clang11_() {
  echo "install clang11"
  wget https://apt.llvm.org/llvm.sh
  chmod +x llvm.sh
  sudo ./llvm.sh 11
  export PATH=/usr/lib/llvm-11/bin:/data/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/node/bin:/usr/local/go/bin:/snap/bin
}

function install_clang11() {
  if command -v clang &> /dev/null; then
    CLANG_VERSION=$(clang --version | grep version | sed -e 's/Ubuntu clang version\ \(.*\)\-++.*/\1/')
    if [ ${CLANG_VERSION} < "11.0.0" ]; then
      install_clang11_
    fi
  else
    install_clang11_
  fi
}

function install_go_() {
  echo "install go"
  while true; do
    read -p "your go binary will be replace with go go1.15.2, are you sure? [Yn]" yn
    case $yn in
        [Yy]* ) 
        curl -O https://dl.google.com/go/go1.15.2.linux-amd64.tar.gz
        rm -rf /usr/local/go && tar -C /usr/local -xzf go1.15.2.linux-amd64.tar.gz
        break;;
        [Nn]* ) break;;
        * ) echo "Please answer yes or no.";;
    esac
  done
}

function install_go() {
  if command -v go &> /dev/null; then
  v=`go version | { read _ _ v _; echo ${v#go}; }`
  if [ v < "1.11.0" ]; then
  install_go_
  fi
  else
  install_go_
  fi
}

function setup_go_mod() {
  echo "setup env"
  export GO111MODULE="on" 
  export GOPROXY="https://goproxy.io,direct"
}

function install_clangd() {
  echo "install clangd"
  sudo apt-get install clangd-9
}

function build_envoy() {
  echo "build envoy"
  cd envoy && CC=clang CXX=clang++ bazel build //source/exe:envoy-static
}

prepare_install
install_bazel
install_clang11
install_go
setup_go_mod
install_clangd
download_envoy
build_envoy
