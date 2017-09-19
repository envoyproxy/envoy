FROM centos/devtoolset-6-toolchain-centos7

COPY ./build_container.sh ./build_and_install_deps.sh ./target_recipes.bzl ./recipe_wrapper.sh ./Makefile /
COPY ./build_recipes/*.sh /build_recipes/

USER root
RUN cd / && ./build_container.sh
