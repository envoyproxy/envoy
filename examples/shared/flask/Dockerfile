FROM python:3.10.6-slim-bullseye@sha256:37ed2eb4cee1959ea829f7bc158980b99a46b998347e11a270a1ebfc02251923

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
