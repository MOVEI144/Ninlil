#!/usr/bin/env bash
set -euo pipefail

readonly EXPECTED_BRANCH="pi/p0-delivery-contract-implementation-20260829"
readonly SESSION_NAME="${NINLIL_PI_SESSION:-ninlil-p0}"
readonly WINDOW_NAME="pi"
readonly PI_PROVIDER="openai-codex"
readonly PI_MODEL="gpt-5.6-sol"
readonly PI_THINKING="xhigh"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prompt_file="${repo_root}/TASK_P0_IMPLEMENTATION.md"

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

command -v git >/dev/null 2>&1 || fail "git is not installed"
command -v tmux >/dev/null 2>&1 || fail "tmux is not installed"
command -v pi >/dev/null 2>&1 || fail "Pi Coding Agent is not installed or not on PATH"

current_branch="$(git -C "${repo_root}" branch --show-current)"
[[ "${current_branch}" == "${EXPECTED_BRANCH}" ]] || \
    fail "expected branch ${EXPECTED_BRANCH}, found ${current_branch:-detached HEAD}"

[[ -f "${prompt_file}" ]] || fail "missing ${prompt_file}"

if [[ -n "$(git -C "${repo_root}" status --porcelain)" ]]; then
    fail "working tree is not clean; commit or discard changes before starting Pi"
fi

if ! pi --list-models "${PI_PROVIDER}" 2>/dev/null | grep -Fq "${PI_MODEL}"; then
    fail "${PI_PROVIDER}/${PI_MODEL} is not available to this Pi installation or account"
fi

if tmux has-session -t "${SESSION_NAME}" 2>/dev/null; then
    fail "tmux session ${SESSION_NAME} already exists; inspect it instead of replacing running work"
fi

tmux new-session -d \
    -s "${SESSION_NAME}" \
    -n "${WINDOW_NAME}" \
    -c "${repo_root}" \
    "pi --provider ${PI_PROVIDER} --model ${PI_MODEL} --thinking ${PI_THINKING} --name ninlil-p0"

# Wait for the TUI to initialize. The bound is finite; failure leaves the session
# available for inspection rather than destroying possible diagnostic output.
for _ in $(seq 1 20); do
    if tmux list-panes -t "${SESSION_NAME}:${WINDOW_NAME}" >/dev/null 2>&1; then
        break
    fi
    sleep 0.25
done

tmux list-panes -t "${SESSION_NAME}:${WINDOW_NAME}" >/dev/null 2>&1 || \
    fail "Pi tmux pane did not become available"

sleep 2
buffer_name="ninlil-p0-$RANDOM-$$"
tmux load-buffer -b "${buffer_name}" "${prompt_file}"
tmux paste-buffer -d -b "${buffer_name}" -t "${SESSION_NAME}:${WINDOW_NAME}"
tmux send-keys -t "${SESSION_NAME}:${WINDOW_NAME}" Enter

printf 'Started Pi implementation session.\n'
printf '  repo:     %s\n' "${repo_root}"
printf '  branch:   %s\n' "${EXPECTED_BRANCH}"
printf '  tmux:     %s:%s\n' "${SESSION_NAME}" "${WINDOW_NAME}"
printf '  provider: %s\n' "${PI_PROVIDER}"
printf '  model:    %s\n' "${PI_MODEL}"
printf '  thinking: %s\n' "${PI_THINKING}"
printf '\nAttach with: tmux attach -t %s\n' "${SESSION_NAME}"
printf 'Inspect without attaching: tmux capture-pane -pt %s:%s -S -80\n' \
    "${SESSION_NAME}" "${WINDOW_NAME}"
