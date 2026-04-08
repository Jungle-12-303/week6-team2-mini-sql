#!/usr/bin/env bash
set -euo pipefail

if ! command -v cc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  export TZ=Etc/UTC
  apt-get update
  apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    ca-certificates \
    file \
    gdb \
    git \
    make \
    pkg-config
  rm -rf /var/lib/apt/lists/*
fi

make mini_sql
