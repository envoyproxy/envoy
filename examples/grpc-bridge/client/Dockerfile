FROM python:3.10.6-slim@sha256:59129c9fdea259c6a9b6d9c615c065c336aca680ee030fc3852211695a21c1cf

WORKDIR /client

COPY requirements.txt /client/requirements.txt

# Cache the dependencies
RUN pip install --require-hashes -qr /client/requirements.txt

# Copy the sources, including the stubs
COPY client.py /client/grpc-kv-client.py
COPY kv /client/kv

RUN chmod a+x /client/grpc-kv-client.py

# http://bigdatums.net/2017/11/07/how-to-keep-docker-containers-running/
# Call docker exec /client/grpc.py set | get
CMD tail -f /dev/null
