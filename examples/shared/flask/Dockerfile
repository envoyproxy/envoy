FROM python:3.10.5-slim-bullseye@sha256:2ae2b820fbcf4e1c5354ec39d0c7ec4b3a92cce71411dfde9ed91d640dcdafd8

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
