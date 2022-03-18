FROM flask_service:python-3.10-slim-bullseye

COPY --from=envoyproxy/envoy-dev:latest /usr/local/bin/envoy /usr/local/bin/envoy

ADD requirements.txt /tmp/requirements.txt
RUN pip3 install -r /tmp/requirements.txt

ADD ./service.py /code/service.py

ADD ./start_service.sh /usr/local/bin/start_service.sh
RUN chmod u+x /usr/local/bin/start_service.sh
ENTRYPOINT ["/usr/local/bin/start_service.sh"]
