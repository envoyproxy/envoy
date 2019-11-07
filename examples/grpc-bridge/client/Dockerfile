FROM grpc/python

WORKDIR /client

COPY requirements.txt /client/requirements.txt

# Cache the dependencies
RUN pip install -r /client/requirements.txt

# Copy the sources, including the stubs
COPY client.py /client/grpc-kv-client.py
COPY kv /client/kv

# https://stackoverflow.com/questions/1260792/import-a-file-from-a-subdirectory/1260813#1260813
#RUN touch /client/kv/__init__.py

RUN chmod a+x /client/grpc-kv-client.py

# http://bigdatums.net/2017/11/07/how-to-keep-docker-containers-running/
# Call docker exec /client/grpc.py set | get
CMD tail -f /dev/null
