To learn about this sandbox and for instructions on how to run it please head over
to the envoy docs

### Building the Docker image

The `lyft/envoy` image is a docker image made using `build-envoy/Dockerfile-envoy`
it copies a precompiled envoy binary into a docker image, and installs `curl`
and `pip`.

The following steps guide you through building your own envoy binary, and
putting that in a clean ubuntu container.

#### Step 1: Build Envoy

Using `lyft/envoy-buld` you will compile envoy.
This image has all software needed to build envoy. From your envoy directory:

```shell
$ pwd
src/envoy
$ docker run -t -i -v <SOURCE_DIR>:/source lyft/envoy-build:latest /bin/bash -c "cd /source && ci/do_ci.sh normal"
```

That command will take some time to run because it is compiling an envoy binary.

#### Step 2: Build image with only envoy binary

In this step we'll build an image that only has the envoy binary, and none
of the software used to build it.

```shell
$ pwd
src/envoy/
$ docker build -f example/Dockerfile-envoy-image -t envoy .
```

Now you can use this `envoy` image to build the demo if you change the `FROM`
line in `Dockerfile-frontenvoy` and `Dockerfile-service`
