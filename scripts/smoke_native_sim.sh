#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Build every app for native_sim and run each one for a few seconds.
# Useful as a CI signal: catches devicetree/config breakage without needing
# the Presto in front of you.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BOARD="${BOARD:-native_sim/native/64}"
BUILD_ROOT="${BUILD_ROOT:-${ROOT_DIR}/build/native_sim_smoke}"
PYTHON_EXECUTABLE="${PYTHON_EXECUTABLE:-${ROOT_DIR}/.venv/bin/python}"
SMOKE_TIMEOUT_SECONDS="${SMOKE_TIMEOUT_SECONDS:-3}"

if [[ -z "${ZEPHYR_BASE:-}" ]]; then
  echo "ERROR: ZEPHYR_BASE is not set. Point it at your Zephyr tree." >&2
  exit 1
fi

if [[ ! -d "${ZEPHYR_BASE}" ]]; then
  echo "ERROR: ZEPHYR_BASE does not exist: ${ZEPHYR_BASE}" >&2
  exit 1
fi

if [[ ! -x "${PYTHON_EXECUTABLE}" ]]; then
  echo "ERROR: Python executable not found: ${PYTHON_EXECUTABLE}" >&2
  echo "Create a venv at ${ROOT_DIR}/.venv or set PYTHON_EXECUTABLE." >&2
  exit 1
fi

apps=(
  test_leds
  test_buttons
  test_touch
  test_wifi
  kitchen_sink
)

echo "ZEPHYR_BASE       = ${ZEPHYR_BASE}"
echo "BOARD             = ${BOARD}"
echo "BUILD_ROOT        = ${BUILD_ROOT}"
echo "PYTHON_EXECUTABLE = ${PYTHON_EXECUTABLE}"
echo "SMOKE_TIMEOUT     = ${SMOKE_TIMEOUT_SECONDS}s"
echo

for app in "${apps[@]}"; do
  app_dir="${ROOT_DIR}/apps/${app}"
  build_dir="${BUILD_ROOT}/${app}"

  if [[ ! -d "${app_dir}" ]]; then
    echo "ERROR: missing app directory: ${app_dir}" >&2
    exit 1
  fi

  cmake_args=(
    -S "${app_dir}"
    -B "${build_dir}"
    -GNinja
    -DBOARD="${BOARD}"
    -DPython3_EXECUTABLE="${PYTHON_EXECUTABLE}"
  )

  native_conf="${app_dir}/boards/native_sim.conf"
  native_overlay="${app_dir}/boards/native_sim.overlay"

  if [[ -f "${native_conf}" ]]; then
    cmake_args+=("-DEXTRA_CONF_FILE=${native_conf}")
  fi

  if [[ -f "${native_overlay}" ]]; then
    cmake_args+=("-DDTC_OVERLAY_FILE=${native_overlay}")
  fi

  echo "=== [${app}] configure ==="
  rm -rf "${build_dir}"
  cmake "${cmake_args[@]}"

  echo "=== [${app}] build ==="
  cmake --build "${build_dir}" -j"$(nproc)"

  echo "=== [${app}] run (${SMOKE_TIMEOUT_SECONDS}s) ==="
  SDL_VIDEODRIVER=dummy timeout "${SMOKE_TIMEOUT_SECONDS}s" \
    "${build_dir}/zephyr/zephyr.exe" || true
  echo
done

echo "Smoke run complete."
