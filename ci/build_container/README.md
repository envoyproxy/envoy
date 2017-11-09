Envoy's CI has a build run called `build_image`. On a commit to master, `ci/build_container/docker_push.sh`
checks if the commit has changed the `ci/build_container` directory. If there are changes, CI builds a new `envoyproxy/envoy-build`
image. The image is pushed to [dockerhub](https://hub.docker.com/r/envoyproxy/envoy-build/tags/) under `latest` and under the commit sha.

After the PR that changes `ci/build_container` has been merged, and the new image gets pushed,
a second PR is needed to update `ci/envoy_build_sha.sh`. In order to pull the new tagged version of
the build image, change ENVOY_BUILD_SHA [here](https://github.com/envoyproxy/envoy/blob/master/ci/envoy_build_sha.sh).
Any PRs that depend on this image change will have to merge master after the change to `ci/envoy_build_sha.sh` has been merged to master.
