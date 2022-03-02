FROM python:3-alpine

Add requirements.txt /requirements.txt
RUN pip3 install -q -r /requirements.txt
COPY . ./app
CMD ["python3", "/app/service/server.py"]
