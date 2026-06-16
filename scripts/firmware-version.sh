#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

stamp="${SOURCE_DATE_EPOCH:-}"
if [ -n "$stamp" ]; then
    date_part="$(date -u -d "@$stamp" +'%Y%m%d%H%M')"
else
    date_part="$(date -u +'%Y%m%d%H%M')"
fi

if [ -n "${GITHUB_SHA:-}" ]; then
    short_sha="${GITHUB_SHA::7}"
elif git -C "$REPO_ROOT" rev-parse --short=7 HEAD >/dev/null 2>&1; then
    short_sha="$(git -C "$REPO_ROOT" rev-parse --short=7 HEAD)"
else
    short_sha="nogit"
fi

version="meshcore-${date_part}-${short_sha}"

if [ -n "${GITHUB_RUN_NUMBER:-}" ]; then
    version="${version}-run${GITHUB_RUN_NUMBER}"
elif git -C "$REPO_ROOT" diff --quiet --ignore-submodules HEAD -- >/dev/null 2>&1; then
    :
else
    version="${version}-dirty"
fi

printf '%s\n' "$version"

