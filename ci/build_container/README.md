Envoy's CI has a build run called `build_image`. On a commit to master, `ci/docker_push_build.sh`
checks if the commit has changed the `ci/build_container` directory. If there are changes,
`ci/build_container/update_build_container.sh` is ran to build a new `lyft/envoy-build`
image. The image is pushed to dockerhub under `latest` and under the commit sha.