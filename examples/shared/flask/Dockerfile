FROM python:3.10.5-slim-bullseye@sha256:f9f03f46267e182193544299504687e711c623e2a085323138f94ed9b01ce641

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
