FROM lyft/envoy-build:latest

ADD . /source
WORKDIR /source
RUN ["ci/build_push_image.sh"]
RUN ["cp", "/source/build/source/exe/envoy", "/usr/local/bin"]
WORKDIR /
RUN ["rm", "-rf", "/source"]
