FROM python:3.13-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt && groupadd --gid 10001 agora && useradd --uid 10001 --gid 10001 --no-create-home agora && mkdir /run/agora-agent && chown agora:agora /run/agora-agent
COPY LICENSE README.md ./
COPY agora_agent ./agora_agent
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 AGENT_SOCKET=/run/agora-agent/agent.sock
USER 10001:10001
CMD ["python", "-m", "agora_agent.daemon"]
