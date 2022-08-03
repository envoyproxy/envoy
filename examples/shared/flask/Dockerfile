FROM python:3.11.0b5-slim-bullseye@sha256:4a4b00fb6167ced9d3b788efa80743d5bfe2c98e3c4717764d8a3bad7fe89d36

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
