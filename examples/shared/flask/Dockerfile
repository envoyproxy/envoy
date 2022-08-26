FROM python:3.10.6-slim-bullseye@sha256:dff7fd9200421a8c65e020af221a21c8aab784c5c8a8d55c64a095b645209d77

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
