FROM envoyproxy/envoy-dev:latest

RUN mkdir /var/log/envoy/
COPY ./bin/service /usr/local/bin/srv
COPY ./script/grpc_start.sh /etc/grpc_start
CMD /etc/grpc_start

