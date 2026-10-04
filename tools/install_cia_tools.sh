#!/usr/bin/env bash
# Pinned upstream Linux x86_64 releases, installed only into this project.
set -euo pipefail
mkdir -p .tools/bin .tools/unpack
curl -fL --retry 3 https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip -o .tools/unpack/bannertool.zip
curl -fL --retry 3 https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-ubuntu_x86_64.zip -o .tools/unpack/makerom.zip
unzip -qo .tools/unpack/bannertool.zip -d .tools/unpack/bannertool
unzip -qo .tools/unpack/makerom.zip -d .tools/unpack/makerom
banner_binary=$(find .tools/unpack/bannertool -type f -name bannertool -path '*linux*' | head -n 1)
makerom_binary=$(find .tools/unpack/makerom -type f -name makerom | head -n 1)
test -n "$banner_binary"
test -n "$makerom_binary"
install -m 755 "$banner_binary" .tools/bin/bannertool
install -m 755 "$makerom_binary" .tools/bin/makerom
