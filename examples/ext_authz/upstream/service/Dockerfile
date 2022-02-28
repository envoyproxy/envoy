FROM python:3-alpine

RUN pip3 install -q Flask==2.0.3
COPY . ./app
CMD ["python3", "/app/service/server.py"]
