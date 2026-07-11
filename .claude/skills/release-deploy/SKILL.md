---
name: release-deploy
description: Cut a keyboardTD release and reload the game on the remote server (app-server-2 / td.zboina.pl). Use when asked to release, deploy, push a new version to the server, or reload the remote container.
---

# Releasing keyboardTD and reloading app-server-2

Releases are tag-driven; the server only ever runs `ghcr.io/zboinek/keyboardtd:latest`.
Deploying = push a tag, let CI build the image, then pull + restart on the server.

## 1. Tag and push

Tags are bare versions (`0.1.0`, `0.1.1`, ...). Pick the next semver
(`git tag -l` to see existing), then from a clean, pushed `main`:

```sh
git tag <version>
git push origin main --tags
```

This triggers two GitHub Actions workflows:
- **docker** — builds the WebAssembly image and pushes
  `ghcr.io/zboinek/keyboardtd:<version>` and `:latest` (amd64 + arm64).
- **release** — attaches Linux/macOS/Windows terminal binaries to a GitHub
  Release.

## 2. Wait for the docker workflow

The server pulls `:latest`, so reloading before the **docker** workflow
finishes redeploys the *old* image. Watch it:

```sh
gh run watch --exit-status $(gh run list --workflow=docker --limit 1 --json databaseId -q '.[0].databaseId')
```

(Takes several minutes — it cross-compiles for arm64 under QEMU.)

## 3. Reload the server

`app-server-2` is defined in `~/.ssh/config` (zboinek@195.167.155.15:3000).
The game is the `keyboardtd` service in `~/apps/docker-compose.yml`, served
at https://td.zboina.pl behind an nginx-proxy + letsencrypt companion
(don't touch those containers).

```sh
ssh app-server-2 'cd ~/apps && docker compose pull keyboardtd && docker compose up -d keyboardtd'
```

## 4. Verify

```sh
ssh app-server-2 'docker ps --filter name=keyboardtd --format "{{.Image}} {{.Status}}"'
curl -sI https://td.zboina.pl | head -1   # expect HTTP 200
```

Confirm the running image digest changed / the container restarted recently.
If the site serves stale content, it's browser/xterm.js caching — the nginx
proxy itself doesn't cache.
