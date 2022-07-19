FROM python:3.10.5-slim-bullseye@sha256:ac63ff0730358ed061589c374fa871958ba0e796b590741395ff3d5d95177fab

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
