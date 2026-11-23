#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p ~/.config/systemd/user
cp "$here/mini-spotify-bridge.service" ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now mini-spotify-bridge.service
systemctl --user status mini-spotify-bridge.service --no-pager | head -8
