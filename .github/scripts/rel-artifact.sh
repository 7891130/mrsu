#!/usr/bin/env bash
# CI artifact transport over GitHub Release assets.
# Release assets do NOT count against the GitHub Actions artifact/cache storage
# quota (500MB on free private repos), so this replaces actions/upload-artifact
# and actions/download-artifact for the whole build chain.
#
# usage:
#   rel-artifact.sh upload       <name> <path...>   # file(s) or dir -> <name>.tar.gz asset
#   rel-artifact.sh download     <name> <dest-dir>  # restore <dest-dir>/<name>/...
#   rel-artifact.sh download-all <dest-dir>         # restore every package into <dest-dir>
#   rel-artifact.sh purge                           # delete the release for this run
set -euo pipefail

REPO="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY not set}"
TOKEN="${GITHUB_TOKEN:?GITHUB_TOKEN not set}"
TAG="${CI_REL_TAG:-ci-${GITHUB_RUN_ID:?GITHUB_RUN_ID not set}}"
API="https://api.github.com/repos/${REPO}"
UPLOAD_API="https://uploads.github.com/repos/${REPO}"
REL_ID=""

auth_hdr="Authorization: token ${TOKEN}"

ensure_release() {
  local code
  for _ in 1 2 3; do
    code=$(curl -sS -o /tmp/_rel.json -w '%{http_code}' -X POST \
      -H "$auth_hdr" -H "Accept: application/vnd.github+json" \
      -H "Content-Type: application/json" \
      -d "{\"tag_name\":\"${TAG}\",\"name\":\"${TAG}\",\"prerelease\":true,\"body\":\"automated CI artifacts for run ${GITHUB_RUN_ID:-0}\"}" \
      "${API}/releases" || true)
    if [ "$code" = "201" ]; then
      break
    fi
    code=$(curl -sS -o /tmp/_rel.json -w '%{http_code}' \
      -H "$auth_hdr" -H "Accept: application/vnd.github+json" \
      "${API}/releases/tags/${TAG}" || true)
    if [ "$code" = "200" ]; then
      break
    fi
    sleep 5
  done
  REL_ID=$(sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' /tmp/_rel.json 2>/dev/null | head -n 1 || true)
  if [ -z "${REL_ID:-}" ]; then
    echo "rel-artifact: cannot resolve release id for tag ${TAG}" >&2
    cat /tmp/_rel.json >&2 || true
    exit 1
  fi
  echo "rel-artifact: release ${TAG} id=${REL_ID}"
}

cmd_upload() {
  local name="$1"; shift
  [ -n "$name" ] || { echo "upload: name required" >&2; exit 2; }
  [ "$#" -gt 0 ] || { echo "upload: path required" >&2; exit 2; }

  local stage="/tmp/_pkg"
  rm -rf "$stage" "/tmp/${name}.tar.gz"
  mkdir -p "${stage}/${name}"
  local p
  for p in "$@"; do
    if [ -d "$p" ]; then
      cp -a "$p"/. "${stage}/${name}/"
    else
      cp -a "$p" "${stage}/${name}/"
    fi
  done
  tar czf "/tmp/${name}.tar.gz" -C "$stage" "$name"

  ensure_release
  # asset upload MUST go through uploads.github.com, api.github.com returns 404
  local code
  code=$(curl -sS -o /tmp/_up.json -w '%{http_code}' -X POST \
    -H "$auth_hdr" -H "Accept: application/vnd.github+json" \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@/tmp/${name}.tar.gz" \
    "${UPLOAD_API}/releases/${REL_ID}/assets?name=${name}.tar.gz")
  echo "rel-artifact: upload ${name} -> http ${code}"
  if [ "$code" != "201" ]; then
    cat /tmp/_up.json >&2 || true
    exit 1
  fi
  # keep the wheel small: drop the tarball right away
  rm -f "/tmp/${name}.tar.gz"
}

py_fetch() {
  # $1 = asset name (exact) or "__ALL__", $2 = dest dir
  python3 - "$1" "$2" "$TAG" "$REPO" "$TOKEN" <<'PY'
import json, os, sys, urllib.request

target, dest, tag, repo, token = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
hdr = {"Authorization": "token " + token, "Accept": "application/vnd.github+json"}

def api(url, accept=None):
    h = dict(hdr)
    if accept:
        h["Accept"] = accept
    return urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=120)

rel = json.load(api("https://api.github.com/repos/%s/releases/tags/%s" % (repo, tag)))
assets = rel.get("assets", [])
picked = [a for a in assets if target == "__ALL__" or a["name"] == target]
if not picked:
    names = ", ".join(a["name"] for a in assets) or "(none)"
    sys.stderr.write("rel-artifact: asset %s not found; available: %s\n" % (target, names))
    sys.exit(1)

os.makedirs(dest, exist_ok=True)
for a in picked:
    out = os.path.join(dest, a["name"])
    data = api("https://api.github.com/repos/%s/releases/assets/%d" % (repo, a["id"]),
               accept="application/octet-stream").read()
    with open(out, "wb") as f:
        f.write(data)
    print("rel-artifact: downloaded %s (%d bytes)" % (a["name"], len(data)))
PY
}

cmd_download() {
  local name="$1" dest="$2"
  py_fetch "${name}.tar.gz" /tmp/_dl
  mkdir -p "$dest"
  tar xzf "/tmp/_dl/${name}.tar.gz" -C "$dest"
  rm -rf /tmp/_dl
}

cmd_download_all() {
  local dest="$1"
  py_fetch "__ALL__" /tmp/_dl
  mkdir -p "$dest"
  local f
  for f in /tmp/_dl/*.tar.gz; do
    [ -e "$f" ] || continue
    tar xzf "$f" -C "$dest"
  done
  rm -rf /tmp/_dl
}

cmd_purge() {
  curl -sS -o /dev/null -w 'rel-artifact: purge http %{http_code}\n' -X DELETE \
    -H "$auth_hdr" "${API}/releases/tags/${TAG}" || true
}

action="${1:-}"; shift || true
case "$action" in
  upload)       cmd_upload "$@" ;;
  download)     cmd_download "$@" ;;
  download-all) cmd_download_all "$@" ;;
  purge)        cmd_purge ;;
  *) echo "usage: $0 upload|download|download-all|purge ..." >&2; exit 2 ;;
esac
