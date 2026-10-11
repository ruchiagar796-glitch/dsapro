# Builds the C++ backend in the cloud and runs it. You never compile anything yourself.
# ---------- stage 1: build ----------
FROM gcc:13-bookworm AS build
# libsodium-dev provides the Argon2id password hashing; its STATIC library is linked in, so the
# final image needs no extra packages.
RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake ninja-build ca-certificates libsodium-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY include include
COPY src src
COPY tests tests
# The HTTP tests serve the real pages, so the frontend must be present while testing.
COPY frontend frontend
# CMake downloads the two header-only libraries (SHA-256 verified).
# The tests run during the build: if any test fails, the deployment fails instead of going live broken.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
 && cmake --build build --target bank_server bank_tests \
 && ./build/bank_tests

# ---------- stage 2: small runtime image ----------
FROM debian:bookworm-slim
RUN useradd --system --uid 10001 bank && mkdir -p /app /data && chown bank /data
WORKDIR /app
COPY --from=build /src/build/bank_server ./bank_server
COPY frontend ./frontend
COPY docker-entrypoint.sh ./docker-entrypoint.sh
RUN chmod +x ./docker-entrypoint.sh
# Listen on all addresses inside the container. Set BMS_ADMIN_PASSWORD (secret) in the host's
# environment settings to create the first administrator.
ENV BMS_HOST=0.0.0.0 BMS_DATA_DIR=/data PORT=8080 BMS_IN_CONTAINER=1
EXPOSE 8080
# The entry script fixes ownership of a freshly mounted disk, then runs the server as the
# unprivileged "bank" user (never as root).
ENTRYPOINT ["./docker-entrypoint.sh"]
