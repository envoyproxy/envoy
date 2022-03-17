FROM flask:python-3.10-slim-bullseye

COPY --from=envoyproxy/envoy-dev:latest /usr/local/bin/envoy /usr/local/bin/envoy

RUN apt-get update && apt-get -q install --no-install-recommends -y curl

#
# for discussion on jaeger binary compatibility, and the source of the file, see here:
#  https://github.com/envoyproxy/envoy/issues/11382#issuecomment-638012072
#
RUN echo "4a7d17d4724ee890490bcd6cfdedb12a02316a3d33214348d30979abd201f1ca  /usr/local/lib/libjaegertracing_plugin.so" > /tmp/checksum \
         && curl -Ls https://github.com/envoyproxy/misc/releases/download/jaegertracing-plugin/jaegertracing-plugin-centos.tar.gz \
              | tar zxf - -C /usr/local/lib \
         && mv /usr/local/lib/libjaegertracing.so.0.4.2 /usr/local/lib/libjaegertracing_plugin.so \
         && sha256sum -c /tmp/checksum \
         && rm /tmp/checksum

ADD requirements.txt /tmp/requirements.txt
RUN pip3 install -r /tmp/requirements.txt

ADD ./service.py /code/service.py

ADD ./start_service.sh /usr/local/bin/start_service.sh
RUN chmod u+x /usr/local/bin/start_service.sh
ENTRYPOINT ["/usr/local/bin/start_service.sh"]
