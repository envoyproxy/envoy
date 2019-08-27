FROM centos:7

COPY ./build_container_common.sh /
COPY ./build_container_centos.sh /

ENV PATH /opt/rh/rh-git218/root/usr/bin:/opt/rh/devtoolset-7/root/usr/bin:/opt/llvm/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
RUN ./build_container_centos.sh
