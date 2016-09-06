To learn about this sandbox and for instructions on how to run it please head over
to the envoy docs

### Building the Docker image

The `lyft/envoy` image is a docker image made using `build-envoy/Dockerfile-envoy` 
it copies a precompiled envoy binary into a docker image, and installs `curl` 
and `pip`. 

The following steps guide you through building your own envoy binary, and 
putting that in a clean ubuntu container.

#### Step 1: Build Envoy

Using `build-envoy/Dockerfile-build-envoy` you will run a container that will 
compile envoy. This container is based on the `lyft/envoy-build` container, 
which has all software needed to build envoy. From a fresh envoy clone run:

```shell
$ pwd
src/envoy
$ docker build -f example/build-envoy/Dockerfile-build-envoy -t envoybinary .
```

That command will take some time to run, because it is compiling an envoy binary

#### Step 2: Copy the envoy binary

In this step we'll copy the envoy binary from the `envoybinary` docker image to 
the `example/build-envoy` directory. To do this, we need to create a temporary 
container.

```shell
$ pwd 
src/envoy
$ id=$(docker create envoybinary)
$ docker cp $id:envoy/build/source/exe/envoy example/build-envoy/
$ docker rm -v $id
```

#### Step 3: Build image with only envoy binary

In this step we'll build an image that only has the envoy binary, and none
of the software used to build it.

```shell
$ pwd
src/envoy/example/build-envoy
$ docker build -f Dockerfile-envoy -t envoy .
```

Now you can use this `envoy` image to build the demo if you change the `FROM`
line in `Dockerfile-frontenvoy` and `Dockerfile-service`
