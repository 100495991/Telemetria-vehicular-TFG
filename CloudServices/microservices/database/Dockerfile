FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY ingestor.py .

# Sin buffering para que los logs salgan en tiempo real por `docker logs`
ENV PYTHONUNBUFFERED=1

CMD ["python", "ingestor.py"]
