FROM python:3.10.5-slim-bullseye@sha256:cacb3a7cdff9328f9487e1418ba18a2f1863902b2390267f3b98853d02e88214

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
