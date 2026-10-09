FROM python:3.13-slim-bookworm AS model
WORKDIR /build
COPY download_model.py ./
RUN python download_model.py

FROM python:3.13-slim-bookworm
WORKDIR /app
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt \
 && groupadd --gid 10001 agora && useradd --uid 10001 --gid 10001 --create-home agora
COPY --from=model /models /models
COPY embedding.py projection.py server.py ./
USER 10001:10001
EXPOSE 8445
CMD ["python", "server.py"]
