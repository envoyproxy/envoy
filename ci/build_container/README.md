## Steps to publish new image for Envoy 3rd party dependencies

*Currently this can be done only by Envoy team*

After you have made changes to `build_container.sh` and merge them to master:

1.  Checkout master and pull latest changes.
2.  Get the SHA of the master commit of your changes to `build_container.sh`.
3.  From `~/envoy/ci/build_container` run `update_build_container.sh`. **Make sure to have
    DOCKER_USERNAME and DOCKER_PASSWORD environment variables set**. This script will build
    the envoy-build container with the current state of `build_container.sh`, tag the image
    with the SHA provided, and push it to Dockerhub.
4.  After you have done that, update `ci/ci_steps.sh` to pull the new tagged version of `lyft/envoy-build`.
