## Steps to publish new image for Envoy 3rd party dependencies

*Currently this can be done only by Envoy team*

After you have made changes to `build_container.sh` and merge them to master:

1.  Checkout master and pull latest changes.
2.  From `~/envoy/ci/build_container` run `update_build_container.sh`. **Make sure to have
    DOCKER_USERNAME and DOCKER_PASSWORD environment variables set**. This script will build
    the envoy-build container with the current state of `build_container.sh`, tag the image, and push it to Dockerhub:
    ```
    ~/envoy/ci/build_container $ DOCKER_USERNAME=user DOCKER_PASSWORD=pass ./update_build_container.sh
    ```
3.  After you have done that, update `ci/ci_steps.sh` in a new PR to pull the new tagged version of `lyft/envoy-build` during CI runs by updating the `ENVOY_BUILD_SHA` variable. Any PRs that depend on this image change will have to merge master after the change to `ci/ci_steps.sh` has been merged to master.
