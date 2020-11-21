FROM python:3-alpine

RUN pip3 install -q Flask==0.11.1
COPY . ./app
CMD ["python3", "/app/service/server.py"]
