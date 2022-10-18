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

Prepare Test Environment
------------------------
#. Create new user “test” (assume there is already a default user “ubuntu”)::

    ubuntu@node1:~/envoy$ sudo useradd test -m -s /bin/bash -u 10000

#. Set up iptables rule, redirect the traffic from user “test” to envoy::

    ubuntu@node1:~/envoy$ sudo iptables -t nat -A OUTPUT -p tcp  -j REDIRECT --to-ports 1234 -m owner --uid-owner 10000

#. Add a CA to Ubuntu::

    ubuntu@node1:~/envoy$ sudo cp root-ca.pem /usr/local/share/ca-certificates/root-ca.crt
    ubuntu@node1:~/envoy$ sudo update-ca-certificates

   There are a root CA certificate and a root CA private key we have generated in advance under envoy directory. Envoy uses this CA cert/key to mimic server certificates, this makes the curl client trust the certs signed by the specified CA.

#. Start Envoy listening at port 1234(The path of envoy executable file may vary depending on the way of building Envoy)::

    ubuntu@node1:~/envoy$ bazel-bin/source/exe/envoy-static -c splicing_bumping_all.yaml --concurrency 1 --log-level trace

Test TLS splicing and bumping
-----------------------------
Five sample configurations are provided, splicing_bumping_all.yaml is an all-in-one configuration covering all splicing and bumping scenarios.

#. TLS splicing without HTTP CONNECT::

    test@node1:~/envoy$ curl -v https://www.usbank.com/

   The traffic will be redirect to Envoy since we have iptables rule applied, Envoy works like a TCP proxy.

    Expected result: receive a server certificate issued by the upstream.

#. TLS splicing with HTTP CONNECT::

    ubuntu@node1:~/envoy$ curl -v -x 127.0.0.1:1234 https://www.usbank.com/

   "-x" specify the front proxy(Envoy) when accessing the website, Envoy handles the HTTP CONNECT first and let the traffic go through a TCP proxy without decryption.

    Expected result: receive a server certificate issued by the upstream.

#. TLS bumping without HTTP CONNECT::

    test@node1:~/envoy$ curl -v https://www.google.com/

   The traffic will be redirect to envoy since we have iptables rule applied, Envoy mimics the server certificate and does TLS handshake with downstream.

    Expected result: receive a mimic server cert issued by MyRootCA.

#. TLS bumping with HTTP CONNECT::

    ubuntu@node1:~/envoy$ curl -v -x 127.0.0.1:1234 https://www.google.com/

   "-x" specify the front proxy(Envoy) when accessing the website, Envoy handles the HTTP CONNECT first and mimics the server certificate afterwards.

    Expected result: receive a mimic server cert issued by MyRootCA.
