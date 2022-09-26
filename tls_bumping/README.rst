How to Verify Envoy TLS Splicing/Bumping Functionalities
========================================================

Download and Build Envoy
------------------------
#. Download the code to local host::

    $ git clone git@github.com:envoyproxy/envoy.git; cd envoy/
    $ git fetch origin pull/23192/head:tls_splicing_bumping_test
    $ git checkout tls_splicing_bumping_test

#. Build Envoy (Follow the official documentation):

   compile the Envoy binary locally (https://github.com/envoyproxy/envoy/blob/main/bazel/README.md#linux)::

    $ bazel build -c opt envoy

   alternatively, build the Envoy docker image::

    $ ENVOY_DOCKER_BUILD_DIR=~/build ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.release.server_only'
    $ docker build --build-arg TARGETPLATFORM="linux/amd64" -f ci/Dockerfile-envoy -t envoy .
