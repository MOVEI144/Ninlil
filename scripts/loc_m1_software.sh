#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
limit=${NINLIL_M1_SOFTWARE_LOC_LIMIT:-7000}

paths=(
  "$root/include"
  "$root/src"
  "$root/ports/flash"
  "$root/ports/esp32s3"
  "$root/embedded/esp32s3"
  "$root/tests/test_diag_radio.c"
  "$root/tests/test_sx1262_hal.c"
  "$root/tests/test_sx1262_physical.c"
  "$root/tests/esp_stub"
  "$root/tests/m1"
  "$root/scripts/build_esp32s3.sh"
  "$root/scripts/check_esp_syntax.sh"
  "$root/scripts/check_sx126x_driver.sh"
  "$root/scripts/ci_m1.sh"
  "$root/scripts/fetch_sx126x_driver.sh"
  "$root/scripts/loc_m1_software.sh"
  "$root/scripts/static_analysis.sh"
  "$root/third_party/UPSTREAM.toml"
)

files=()
for path in "${paths[@]}"; do
  [[ -e "$path" ]] || continue
  if [[ -d "$path" ]]; then
    while IFS= read -r -d '' file; do
      files+=("$file")
    done < <(
      find "$path" -type f \
        \( -name '*.c' -o -name '*.h' -o -name 'CMakeLists.txt' -o \
           -name '*.sh' -o -name '*.toml' -o -name '*.csv' \) \
        ! -path "$root/third_party/sx126x_driver/src/*" -print0
    )
  else
    files+=("$path")
  fi
done

if ((${#files[@]} == 0)); then
  echo "no M1 first-party files found" >&2
  exit 1
fi

# Milestone budgets are ownership budgets, not cumulative repository globs.
# A file owned by a later, independently reviewed subsystem must not also be
# charged to the original direct-radio M1 budget. The project-wide ceiling in
# loc.sh still counts every first-party source file exactly once.
declare -A excluded=()
add_exclusion() {
  local relative=$1
  relative=${relative%$'\r'}
  [[ -n "$relative" && "$relative" != \#* ]] || return 0
  excluded["$relative"]=1
}

# Legacy P0/runtime files that predate the ownership manifests below.
legacy_exclusions=(
  include/ninlil.h
  include/ninlil_custody.h
  include/ninlil_group.h
  include/ninlil_leaf.h
  include/ninlil_topology.h
  include/ninlil_security_state.h
  src/ninlil.c
  src/ninlil_authorization.c
  src/ninlil_custody.c
  src/ninlil_group.c
  src/ninlil_internal.h
  src/ninlil_journal.h
  src/ninlil_leaf.c
  src/ninlil_policy.c
  src/ninlil_profile.c
  src/ninlil_receive.c
  src/ninlil_replay.c
  src/ninlil_send.c
  src/ninlil_storage.c
  src/ninlil_topology.c
  src/ninlil_wire.c
  src/ninlil_wire.h
  ports/flash/ninlil_flash_journal_file.c
  ports/flash/ninlil_flash_store.c
  ports/flash/ninlil_flash_store.h
  ports/flash/ninlil_security_state.c
  ports/esp32s3/ninlil_flash_admin.c
  ports/esp32s3/ninlil_flash_journal.c
  ports/esp32s3/ninlil_security_partitions.c
  ports/esp32s3/ninlil_security_partitions.h
)
for relative in "${legacy_exclusions[@]}"; do
  add_exclusion "$relative"
done

# These manifests are the canonical ownership ledgers for post-M1 features.
ownership_manifests=(
  scripts/secure_network_files.txt
  scripts/storage_bulk_radio_files.txt
  scripts/autonomous_node_files.txt
  scripts/deployment_lifecycle_files.txt
  scripts/field_deployment_files.txt
)
for manifest in "${ownership_manifests[@]}"; do
  [[ -f "$root/$manifest" ]] || {
    echo "required ownership manifest missing: $manifest" >&2
    exit 1
  }
  while IFS= read -r relative || [[ -n "$relative" ]]; do
    add_exclusion "$relative"
  done < "$root/$manifest"
done

# Adaptive planning, observation and durable fanout were introduced after M1.
# They currently have no single historical ledger, so keep their ownership
# explicit here instead of silently relying on a broad include/src glob.
post_m1_modules=(
  include/ninlil_fanout.h
  include/ninlil_fanout_core.h
  include/ninlil_fanout_store.h
  include/ninlil_link_metrics.h
  include/ninlil_phy_plan.h
  include/ninlil_power_policy.h
  include/ninlil_probe_monitor.h
  include/ninlil_radio_feedback.h
  include/ninlil_route_optimizer.h
  include/ninlil_route_search.h
  src/ninlil_fanout.c
  src/ninlil_fanout_codec.c
  src/ninlil_fanout_core.c
  src/ninlil_fanout_internal.h
  src/ninlil_fanout_store.c
  src/ninlil_fanout_store_internal.h
  src/ninlil_fanout_store_log.c
  src/ninlil_link_metrics.c
  src/ninlil_phy_plan.c
  src/ninlil_power_policy.c
  src/ninlil_probe_monitor.c
  src/ninlil_radio_feedback.c
  src/ninlil_route_optimizer.c
  src/ninlil_route_search.c
)
for relative in "${post_m1_modules[@]}"; do
  add_exclusion "$relative"
done

physical=0
nonblank=0
counted=0
while IFS= read -r -d '' file; do
  relative=${file#"$root/"}
  [[ -z ${excluded["$relative"]+x} ]] || continue
  physical=$((physical + $(wc -l < "$file")))
  nonblank=$((nonblank + $(awk 'NF { n++ } END { print n + 0 }' "$file")))
  counted=$((counted + 1))
done < <(printf '%s\0' "${files[@]}" | sort -zu)

if ((counted == 0)); then
  echo "no M1-owned first-party files found" >&2
  exit 1
fi

printf 'M1 software files: %d\n' "$counted"
printf 'M1 software physical lines: %d\n' "$physical"
printf 'M1 software nonblank lines: %d / %d\n' "$nonblank" "$limit"
if ((nonblank > limit)); then
  echo "M1 software line budget exceeded" >&2
  exit 1
fi
