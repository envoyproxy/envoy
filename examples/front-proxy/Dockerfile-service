FROM lyft/envoy:latest

RUN apt-get update && apt-get -q install -y \
    curl \
    python-pip
RUN pip install -q Flask==0.11.1
RUN mkdir /code
ADD ./service.py /code
ADD ./start_service.sh /usr/local/bin/start_service.sh
RUN chmod u+x /usr/local/bin/start_service.sh
ENTRYPOINT /usr/local/bin/start_service.sh
