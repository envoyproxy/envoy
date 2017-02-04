## Steps to publish new image for Envoy 3rd party dependencies

*Currently this can be done only by Envoy team*

* Modify [build_container.sh](build_container.sh) to include steps for building 3rd party library.
* Build image locally by running `docker build --rm -t lyft/envoy-build:latest .` from this directory.
* Login into docker hub by running `docker login`. Make sure to create account beforehand and get that added to Lyft team.
* Publish image by running `docker push lyft/envoy-build:latest`.
