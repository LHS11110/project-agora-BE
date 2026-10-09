FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /workspace

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        libcpp-httplib-dev \
        freetds-dev \
        libssl-dev \
        nlohmann-json3-dev \
        pkg-config \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

COPY LICENSE THIRD_PARTY_LICENSES.md ./
COPY cpp/ cpp/

RUN cmake -S cpp -B /tmp/agora-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
    && cmake --build /tmp/agora-build --target agora_cpp_server --parallel 2 \
    && cmake --install /tmp/agora-build --prefix /opt/agora

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        libcpp-httplib0.14t64 \
        libsybdb5 \
        libssl3t64 \
        zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 agora \
    && useradd --system --uid 10001 --gid agora --no-create-home --shell /usr/sbin/nologin agora \
    && mkdir -p /etc/freetds

COPY --from=build /opt/agora/ /opt/agora/
COPY --from=build /workspace/cpp/config/freetds-strict.conf.example /etc/freetds/freetds.conf
RUN ldd /opt/agora/bin/agora_cpp_server | tee /tmp/agora-cpp-ldd \
    && ! grep -q 'not found' /tmp/agora-cpp-ldd \
    && rm /tmp/agora-cpp-ldd

ENV PATH="/opt/agora/bin:${PATH}"
USER 10001:10001
EXPOSE 8000 8002
ENTRYPOINT ["agora_cpp_server"]
