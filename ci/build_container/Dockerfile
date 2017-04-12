FROM ubuntu:xenial
COPY ./build_container.sh ./print_recipe_deps.sh ./build_and_install_deps.sh ./recipes.bzl ./Makefile /
COPY ./build_recipes/*.sh /build_recipes/
RUN ./build_container.sh
