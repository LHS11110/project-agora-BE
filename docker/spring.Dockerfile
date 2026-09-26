FROM eclipse-temurin:26-jdk AS build

WORKDIR /workspace

COPY spring/gradlew spring/gradlew
COPY spring/gradle/ spring/gradle/
COPY spring/settings.gradle spring/build.gradle spring/
COPY spring/src/ spring/src/
COPY LICENSE THIRD_PARTY_LICENSES.md ./
COPY cpp/third_party/uWebSockets/LICENSE cpp/third_party/uWebSockets/LICENSE

RUN chmod +x spring/gradlew \
    && ./spring/gradlew --no-daemon -p spring bootJar

FROM eclipse-temurin:26-jre

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 agora \
    && useradd --system --uid 10001 --gid agora --no-create-home --shell /usr/sbin/nologin agora

WORKDIR /app
COPY --from=build --chown=10001:10001 /workspace/spring/build/libs/frelog-0.0.1-SNAPSHOT.jar /app/app.jar

USER 10001:10001
EXPOSE 8080
ENTRYPOINT ["java", "-jar", "/app/app.jar"]
