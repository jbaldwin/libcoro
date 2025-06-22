FROM ubuntu:22.04

RUN apt update && apt install -y \
    mingw-w64 \
    cmake \
    ninja-build \
    zip \
    git \
    g++ \
    build-essential \
    && apt clean

ARG UID=1000
ARG GID=1000

RUN groupadd -g ${GID} builder && \
    useradd -m -u ${UID} -g ${GID} -s /bin/bash builder

RUN apt update && apt install -y \
    mingw-w64 \
    cmake \
    ninja-build \
    build-essential

USER builder
WORKDIR /home/builder/project