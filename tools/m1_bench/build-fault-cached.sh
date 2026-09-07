#!/usr/bin/env bash
set -euo pipefail
commit=$1
variant=$2
artifact=$3
case "$variant" in fault-a-init|fault-a-resp|fault-b-init|fault-b-resp|recovery-a-init|recovery-b-resp) ;; *) exit 2;; esac
case "$artifact" in fault-*|recovery-*) ;; *) exit 2;; esac
git config --global --add safe.directory /source
cd /project
test "$(git status --porcelain)" = '?? third_party/sx126x_driver/'
git fetch /source "$commit"
git checkout --detach "$commit"
test "$(git status --porcelain)" = '?? third_party/sx126x_driver/'
./scripts/check_sx126x_driver.sh
if ! cmp -s "/evidence/$variant/sdkconfig" /tmp/diag-sdkconfig; then
  cp "/evidence/$variant/sdkconfig" /tmp/diag-sdkconfig
fi
idf.py -C embedded/esp32s3 -B /tmp/diag-build \
  -D SDKCONFIG=/tmp/diag-sdkconfig \
  -D "SDKCONFIG_DEFAULTS=/project/embedded/esp32s3/sdkconfig.defaults;/evidence/$variant/extra.defaults" build
mkdir -p "/evidence/$artifact"
cp /tmp/diag-sdkconfig "/evidence/$artifact/sdkconfig"
cp "/evidence/$variant/extra.defaults" "/evidence/$artifact/extra.defaults"
cp /tmp/diag-build/ninlil_m1.bin /tmp/diag-build/flasher_args.json "/evidence/$artifact/"
cp /tmp/diag-build/bootloader/bootloader.bin "/evidence/$artifact/"
cp /tmp/diag-build/partition_table/partition-table.bin "/evidence/$artifact/"
cp third_party/sx126x_driver/PROVENANCE "/evidence/$artifact/semtech-provenance.txt"
git rev-parse HEAD > "/evidence/$artifact/source-commit.txt"
cd "/evidence/$artifact"
sha256sum *.bin sdkconfig extra.defaults flasher_args.json > SHA256SUMS
echo "LOCAL_DELIVERY_BUILD_PASS artifact=$artifact; not flashed or RF tested"
