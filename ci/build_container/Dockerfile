FROM ubuntu:xenial
COPY ./build_container.sh ./build_and_install_deps.sh ./Makefile /
COPY ./build_recipes/*.sh /build_recipes/
RUN ./build_container.sh
