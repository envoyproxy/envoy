FROM python:3.10.6-slim-bullseye@sha256:2124d4f8ccbd537500de16660a876263949ed9a9627cfb6141f418d36f008e9e

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
