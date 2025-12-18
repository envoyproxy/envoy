![Envoy Logo](https://github.com/envoyproxy/artwork/blob/main/PNG/Envoy_Logo_Final_PANTONE.png)

[Cloud-native high-performance edge/middle/service proxy](https://www.envoyproxy.io/)

# Envoy OpenSSL

This README deals with the specifics of this [envoyproxy/envoy-openssl](https://github.com/envoyproxy/envoy-openssl) repository, describing primarily how it differs from the regular [envoyproxy/envoy](https://github.com/envoyproxy/envoy) repository. The full README for the regular [envoyproxy/envoy](https://github.com/envoyproxy/envoy) repository can be found [here](https://github.com/envoyproxy/envoy/blob/main/README.md). 

## Repository Structure

This repository is a copy of the regular [envoyproxy/envoy](https://github.com/envoyproxy/envoy)
repository, with additions & modifications that enable Envoy to be built on OpenSSL rather than
BoringSSL. In addition to the regular Envoy repository structure, already described in
[REPO_LAYOUT.md](REPO_LAYOUT.md), this repository has the following additions & modifications that
are specific to building Envoy on OpenSSL:

* `bssl-compat` This additional directory contains the BoringSSL Compatability Layer implementation. This provides an implementation of the BoringSSL API on top of the OpenSSL libraries.
* `openssl` This additional directory contains config & script files for building Envoy on the `bssl-compat` library rather than on BoringSSL. Where possible, these scripts are minimal wrappers, and delegate most of their behavior to the corresponding scripts in the regular envoy `ci` directory.
* `WORKSPACE` This is the regular envoy `WORKSPACE` file with an additional `local_repository` declaration for the `bssl-compat` library.

## Branching

It is intended that this repository contains the same `release/v1.xx` branch structure as the
regular envoy repository, starting from `release/v1.26`. Each of those branches is a copy of the
identically named branch from the regular [envoyproxy/envoy](https://github.com/envoyproxy/envoy)
repository, with the addition of:

* The additional script & config files, required to build on OpenSSL, as described above.
* Modifications to envoy source code that cannot be hidden in the `bssl-compat` layer.

Note that the initial `release/v1.26` branch is *not* intended for production.
It is anticipated that `release/v1.28` will be the first branch to reach production.

## Synchronizing

Since this repository is primarily a copy of the [envoyproxy/envoy](https://github.com/envoyproxy/envoy) repository,
the process of keeping this repository in sync with the regular envoy repository is automated.

This automation is implemented by the
[envoy-sync-scheduled.yaml](.github/workflows/envoy-sync-scheduled.yaml)
workflow which executes on a daily schedule. For each of the branches listed in
the workflow's strategy matrix, a merge is attempted from the [envoyproxy/envoy](https://github.com/envoyproxy/envoy)
repository into the identically named branch in this repository.

- If the merge is successful, it:
  - pushes the feature branch to the repository
  - creates the associated pull request if it doesn't already exist
  - closes the associated issue if it already exists
- If the merge is unsuccessful, it:
  - leaves the associated pull request untouched if it already exists
  - creates the associated issue issue if it doesn't already exist
  - adds a comment on the associated issue to describe the merge fail

Note that the feature branches created by this automation are named
`auto-merge-<branch-name>`. These repeatable names allow the workflow to reuse &
update existing branches and pull requests, rather than creating new ones each time.

## Building

The process for building envoy-openssl is very similar to building regular envoy, wherever possible
reusing the same builder image and the same scripts, and the same steps.

Building the envoy-openssl project is done in a build container which is based on the regular envoy
build container, but with some additional requirements installed, including OpenSSL 3.0.x. This build
container is launched using the the `openssl/run_envoy_docker.sh` script, which handles some openssl
specific config and then passes control to the regular `ci/run_envoy_docker.sh` script.

Building & running tests, and building the envoy binary itself, is done using the regular
`ci/do_ci.sh` script.

Although the regular `ci/do_ci.sh` script supports many options for building & testing different
variants of envoy, as descibed in [ci/README](ci/README.md), including the use of various sanitizers,
the envoy-openssl project has so far only been built and tested using the `debug` options described
below. All of the other `ci/do_ci.sh` options that are described in the regular envoy documentation
[here](https://github.com/envoyproxy/envoy/tree/main/ci#readme) _may_ work but have not been tested.

To build the envoy executable and run specified tests, in debug mode:
```bash
./openssl/run_envoy_docker.sh './ci/do_ci.sh debug //test/extensions/transport_sockets/tls/...'
```

To build just the envoy executable, in debug mode:
```bash
./openssl/run_envoy_docker.sh './ci/do_ci.sh debug.server_only'
```

After running these build commands, the resulting envoy executable can be found in the host's file
system at `/tmp/envoy-docker-build/envoy/x64/source/exe/envoy/envoy`. Note that you can place the
build artifacts at a different location on the host by setting ENVOY_DOCKER_BUILD_DIR environment
variable _before_ invoking the `openssl/run_envoy_docker.sh` script. For example, running the
following command would put the build artifact in `/build/envoy/x64/source/exe/envoy/envoy`:
```bash
ENVOY_DOCKER_BUILD_DIR=/build ./openssl/run_envoy_docker.sh './ci/do_ci.sh debug.server_only'
```

Note that, in addition to running the `do_ci.sh` script directly in batch mode, as done in the examples
above, the `openssl/run_envoy_docker.sh` script can also be used to run an interactive shell, which
can be more convenient, for example when repeatedly building & running tests:

```bash
host $ ./openssl/run_envoy_docker.sh bash

container $ ./ci/do_ci.sh debug //test/extensions/transport_sockets/tls/...
container $ ./ci/do_ci.sh debug //test/common/runtime/...
```

## Running Envoy

When running the envoy executable in the build container, by default it will fail, with the following error
message, bacause the build image only has OpenSSL 1.1.x installed, but the envoy executable needs to load
and use OpenSSL 3.0.x libraries:

```bash
$ /build/envoy/x64/source/exe/envoy/envoy --version
Expecting to load OpenSSL version 3.0.x but got 1.1.6
```

To ensure that envoy loads the OpenSSL 3.0.x libraries, their path needs to be prepended to `LD_LIBRARY_PATH` before it is executed:
```bash
$ LD_LIBRARY_PATH=$OPENSSL_ROOT_DIR/lib64:$LD_LIBRARY_PATH /build/envoy/x64/source/exe/envoy/envoy --version
/build/envoy/x64/source/exe/envoy/envoy  version: dcd3e1c50ace27b14441fc8b28650b62c0bf2dd2/1.26.8-dev/Modified/DEBUG/BoringSSL
```
