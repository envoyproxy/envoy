Envoy's CI has a build run called `build_image`. On a commit to master, `ci/build_container/docker_push.sh`
checks if the commit has changed the `ci/build_container` directory. If there are changes, CI builds a new `lyft/envoy-build`
image. The image is pushed to dockerhub under `latest` and under the commit sha.