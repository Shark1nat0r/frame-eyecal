FROM docker.io/library/ubuntu:24.04
COPY build-deps.txt /tmp/
RUN apt-get update && xargs -a /tmp/build-deps.txt apt-get install -y --no-install-recommends && rm -rf /var/lib/apt/lists/*
