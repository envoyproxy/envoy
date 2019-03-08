# Sendgrid Envoy Proxy

It's a fork from `envoyproxy/envoy`, with custom changes.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes. See deployment for notes on how to deploy the project on a live system.

### Installing
```
cd envoy-source-code/ci/build_container
export MY_TAG=$(git rev-parse HEAD)
IMAGE_NAME="sendgrid/envoy-build-ubuntu" CIRCLE_SHA1=$MY_TAG ./docker_build.sh
cd ../..
IMAGE_NAME="sendgrid/envoy-build-ubuntu" IMAGE_ID=$MY_TAG ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.release.server_only'
```

#### FAQ
 - *Compilation error*

  Increase the docker server memory limit to 6GB.

