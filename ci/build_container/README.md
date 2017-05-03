Envoy's CI has a build run called `build_image`. On a commit to master, `ci/build_container/docker_push.sh`
checks if the commit has changed the `ci/build_container` directory. If there are changes, CI builds a new `lyft/envoy-build`
image. The image is pushed to dockerhub under `latest` and under the commit sha.

After the PR that changes `ci/build_container` has been merged, and the new image gets pushed,
a second PR is needed to update `ci/ci_steps.sh`. In order to pull the new tagged version of
the build image, change ENVOY_BUILD_SHA [here](https://github.com/lyft/envoy/blob/master/ci/ci_steps.sh#L2). Any PRs that depend on this image change will have to merge master after the change to ci/ci_steps.sh has been merged to master.