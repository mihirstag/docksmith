#!/bin/bash
set -euo pipefail

echo "=== Docksmith Ubuntu Setup Script ==="

echo "1. Installing Dependencies..."
sudo apt-get update
sudo apt-get install -y build-essential cmake libstdc++-11-dev busybox-static python3

echo "2. Building Docksmith..."
cmake -S . -B build
cmake --build build --parallel $(nproc)

echo "3. Setting up Offline Base Image (~/.docksmith)..."
STATE_DIR="${HOME}/.docksmith"
LAYERS_DIR="${STATE_DIR}/layers"
IMAGES_DIR="${STATE_DIR}/images"
STAGING_DIR="$(mktemp -d)"
LAYER_TMP="$(mktemp)"

cleanup() {
    rm -rf "${STAGING_DIR}"
    rm -f "${LAYER_TMP}"
}
trap cleanup EXIT

mkdir -p "${LAYERS_DIR}" "${IMAGES_DIR}" "${STAGING_DIR}/bin" "${STAGING_DIR}/tmp"

BUSYBOX_BIN="$(command -v busybox || true)"
if [ -z "${BUSYBOX_BIN}" ]; then
    echo "Error: busybox was not found after installation."
    exit 1
fi

cp "${BUSYBOX_BIN}" "${STAGING_DIR}/bin/busybox"
chmod +x "${STAGING_DIR}/bin/busybox"

(
    cd "${STAGING_DIR}/bin"
    for app in sh cat echo printf ls; do
        ln -s busybox "${app}"
    done
)

tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner \
    -cf "${LAYER_TMP}" \
    -C "${STAGING_DIR}" .

LAYER_DIGEST_HEX="$(sha256sum "${LAYER_TMP}" | awk '{print $1}')"
LAYER_DIGEST="sha256:${LAYER_DIGEST_HEX}"
LAYER_FINAL="${LAYERS_DIR}/${LAYER_DIGEST_HEX}.tar"
mv "${LAYER_TMP}" "${LAYER_FINAL}"

LAYER_SIZE="$(stat -c%s "${LAYER_FINAL}")"
CREATED="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
MANIFEST_PATH="${IMAGES_DIR}/base_local.json"

python3 - "${MANIFEST_PATH}" "${LAYER_DIGEST}" "${LAYER_SIZE}" "${CREATED}" <<'PY'
import hashlib
import json
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
layer_digest = sys.argv[2]
layer_size = int(sys.argv[3])
created = sys.argv[4]

manifest = {
    "name": "base",
    "tag": "local",
    "digest": "",
    "created": created,
    "config": {
        "Env": [],
        "Cmd": ["/bin/sh"],
        "WorkingDir": "/",
    },
    "layers": [
        {
            "digest": layer_digest,
            "size": layer_size,
            "createdBy": "<base layer preload>",
        }
    ],
}

canonical = json.dumps(manifest, separators=(",", ":"), sort_keys=True)
manifest["digest"] = "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()

manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
PY

echo "=== Setup Complete! ==="
echo "You can now run commands like:"
echo "sudo HOME=\$HOME ./build/docksmith images"
echo "sudo HOME=\$HOME ./build/docksmith build -t sample:latest sample-app"
echo "sudo HOME=\$HOME ./build/docksmith run sample:latest"
