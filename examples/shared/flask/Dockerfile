FROM python:3.10-slim-bullseye@sha256:c853c4bce75d939c4d5b2892753503ba3c3125282392ad58084e77f68862c7eb

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
