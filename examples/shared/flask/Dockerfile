FROM python:3.10.5-slim-bullseye@sha256:298c9915def292fb619a1c402e83e204aec41e56474411fdb0d198c65f235a0f

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
