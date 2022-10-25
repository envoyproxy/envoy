FROM python:3.11.0-slim@sha256:f8cc89f5e47347703ec0c2b755464d7db2fa16f255ab860c4b24ba6ef2402020

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
