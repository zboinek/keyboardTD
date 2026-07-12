# Build the WebAssembly bundle inside the Emscripten image, the terminal
# game + SSH frontend in their own stages, then serve everything from one
# nginx-based image. No local toolchains needed:
#
#   docker build -t keyboardtd .
#   docker run --rm -p 8080:80 -p 5555:5555 keyboardtd
#   open http://localhost:8080        # browser build
#   ssh -p 5555 localhost             # terminal build, any username

# emsdk only publishes amd64 images; the wasm it produces is platform-
# independent, so pinning the build stage is safe (and required for
# multi-arch image builds).
FROM --platform=linux/amd64 emscripten/emsdk:3.1.64 AS build
WORKDIR /src
COPY Makefile ./
COPY src ./src
COPY web ./web
RUN make web

# The native ncurses game, built against musl so it runs in the alpine
# runtime stage. This one must compile on the target platform (C++).
FROM alpine:3.20 AS gamebuild
RUN apk add --no-cache build-base ncurses-dev
WORKDIR /src
COPY Makefile ./
COPY src ./src
RUN make CXX=g++ keyboardtd

# The SSH frontend (server/ssh). Pure Go, so cross-compile from the build
# platform — no QEMU needed for this stage.
FROM --platform=$BUILDPLATFORM golang:1.23-alpine AS sshbuild
ARG TARGETOS TARGETARCH
WORKDIR /src
COPY server/ssh/go.mod server/ssh/go.sum ./
RUN go mod download
COPY server/ssh/ ./
RUN CGO_ENABLED=0 GOOS=$TARGETOS GOARCH=$TARGETARCH go build -ldflags='-s -w' -o /ktd-ssh .

FROM nginx:alpine
# python3: hall-of-fame API sidecar. libstdc++ + ncurses-libs + terminfo:
# the terminal game served over SSH (full terminfo so exotic client TERMs
# still work; libgcc comes in via libstdc++'s dependency).
RUN apk add --no-cache python3 libstdc++ ncurses-libs ncurses-terminfo ncurses-terminfo-base
COPY --from=build /src/dist/web /usr/share/nginx/html
COPY --from=gamebuild /src/keyboardtd /app/keyboardtd
COPY --from=sshbuild /ktd-ssh /app/ktd-ssh
COPY server/leaderboard.py /app/leaderboard.py
COPY server/nginx.conf /etc/nginx/conf.d/default.conf
COPY server/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
# Hall-of-fame SQLite and the SSH host key live here; mount a volume so
# scores survive redeploys and clients don't see host-key warnings.
VOLUME /data
EXPOSE 80 5555
ENTRYPOINT ["/entrypoint.sh"]
