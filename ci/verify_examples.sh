#!/bin/bash

set -e

verify() {
  echo $1
  CONTAINER_ID="$(docker ps -aqf name=$1)"
  if [ "false" == "$(docker inspect -f {{.State.Running}} ${CONTAINER_ID})" ]
  then
    echo "error: $1 not running"
    exit 1
  fi
}

# Test front proxy example
cd examples/front-proxy
docker-compose up --build -d
for CONTAINER_NAME in "frontproxy_front-envoy" "frontproxy_service1" "frontproxy_service2"
do
  verify $CONTAINER_NAME
done
cd ../

# Test grpc bridge example
# install go
GO_VERSION="1.14.4"
curl -O https://storage.googleapis.com/golang/go$GO_VERSION.linux-amd64.tar.gz
tar -xf go$GO_VERSION.linux-amd64.tar.gz
sudo mv go /usr/local
export PATH=$PATH:/usr/local/go/bin
export GOPATH=$HOME/go
mkdir -p $GOPATH/src/github.com/envoyproxy/envoy/examples/
cp -r grpc-bridge $GOPATH/src/github.com/envoyproxy/envoy/examples/
# build example
cd $GOPATH/src/github.com/envoyproxy/envoy/examples/grpc-bridge
./script/bootstrap
./script/build
# verify example works
docker-compose up --build -d
for CONTAINER_NAME in "grpcbridge_python" "grpcbridge_grpc"
do
  verify $CONTAINER_NAME
done
