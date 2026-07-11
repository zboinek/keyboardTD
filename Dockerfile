# Build the WebAssembly bundle inside the Emscripten image, then serve the
# static result with nginx. No local emsdk needed:
#
#   docker build -t keyboardtd .
#   docker run --rm -p 8080:80 keyboardtd
#   open http://localhost:8080

# emsdk only publishes amd64 images; the wasm it produces is platform-
# independent, so pinning the build stage is safe (and required for
# multi-arch image builds).
FROM --platform=linux/amd64 emscripten/emsdk:3.1.64 AS build
WORKDIR /src
COPY Makefile ./
COPY src ./src
COPY web ./web
RUN make web

FROM nginx:alpine
RUN apk add --no-cache python3
COPY --from=build /src/dist/web /usr/share/nginx/html
COPY server/leaderboard.py /app/leaderboard.py
COPY server/nginx.conf /etc/nginx/conf.d/default.conf
COPY server/entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
# Hall-of-fame SQLite lives here; mount a volume to survive redeploys.
VOLUME /data
EXPOSE 80
ENTRYPOINT ["/entrypoint.sh"]
