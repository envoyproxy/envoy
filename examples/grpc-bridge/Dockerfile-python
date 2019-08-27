FROM envoyproxy/envoy-dev:latest

RUN apt-get update
RUN apt-get -q install -y python-dev \
    python-pip
ADD ./client /client
RUN pip install -r /client/requirements.txt
RUN chmod a+x /client/client.py
RUN mkdir /var/log/envoy/
CMD /usr/local/bin/envoy -c /etc/s2s-python-envoy.yaml
