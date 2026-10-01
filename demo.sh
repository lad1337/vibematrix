#!/bin/bash
# demo.sh [DIR]: generate endless file activity for vibematrix to visualise, e.g. to record it.
#
#   ./demo.sh /tmp/demo &      # or in a second terminal
#   vibematrix /tmp/demo
#
# Cycles through phases (~45s per round) so a recording shows every kind of reaction:
# quiet (smoke2 fades to grey), trickle of small edits, burst of new files, refactor,
# cleanup (deletions), one huge file. Without DIR it uses a temp dir and removes it on exit.
# SPEED=2 ./demo.sh runs twice as fast.
set -u
SPEED=${SPEED:-1}

if [ $# -ge 1 ]; then
    dir=$1
    owned=0
else
    dir=$(mktemp -d /tmp/vibematrix-demo.XXXXXX)
    owned=1
fi
mkdir -p "$dir/src" "$dir/docs" "$dir/assets"
scratch=$(mktemp -d /tmp/vibematrix-scratch.XXXXXX) # temp files live outside the watched dir

cleanup() {
    rm -rf "$scratch"
    [ "$owned" = 1 ] && rm -rf "$dir"
    exit 0
}
trap cleanup INT TERM

say() { printf '\033[2m[demo]\033[0m %s\n' "$*" >&2; }
nap() { sleep "$(awk -v s="$1" -v k="$SPEED" 'BEGIN { print s / k }')"; }

# N random-looking lines
lines() {
    awk -v n="$1" -v seed="$RANDOM$RANDOM" 'BEGIN {
        srand(seed)
        for (i = 0; i < n; i++) printf "%s %d = 0x%08x;\n", (rand() < .5 ? "let" : "var"), i, int(rand() * 4294967295)
    }'
}

files() { find "$dir" -type f; }
count() { files | wc -l | tr -d ' '; }
pick() { files | awk -v seed="$RANDOM" 'BEGIN { srand(seed) } { f[NR] = $0 } END { if (NR) print f[int(rand() * NR) + 1] }'; }
new_name() {
    local sub=(src docs assets) ext=(c md txt js py)
    echo "$dir/${sub[RANDOM % 3]}/file_$RANDOM.${ext[RANDOM % 5]}"
}

# rewrite a fraction of a file's lines in place (no rename, so no temp file shows up in the watch)
rewrite() {
    local f=$1 frac=$2
    awk -v frac="$frac" -v seed="$RANDOM" 'BEGIN { srand(seed) }
        { if (rand() < frac) printf "edited %d 0x%08x;\n", NR, int(rand() * 4294967295); else print }' "$f" >"$scratch/t"
    cat "$scratch/t" >"$f"
}

say "writing to $dir (Ctrl-C to stop)"
for i in $(seq 1 8); do lines $((20 + RANDOM % 80)) >"$(new_name)"; done

while :; do
    say "quiet"
    nap 6

    say "trickle: small edits"
    for i in $(seq 1 8); do
        f=$(pick)
        [ -n "$f" ] && lines $((1 + RANDOM % 4)) >>"$f"
        nap 0.8
    done

    say "burst: new files"
    for i in $(seq 1 25); do
        lines $((30 + RANDOM % 400)) >"$(new_name)"
        nap 0.08
    done
    nap 4

    say "refactor: rewriting lines across files"
    for i in $(seq 1 12); do
        f=$(pick)
        [ -n "$f" ] && rewrite "$f" 0.5
        nap 0.3
    done
    nap 4

    say "cleanup: deleting files"
    n=$(($(count) / 2))
    for i in $(seq 1 "$n"); do
        f=$(pick)
        [ -n "$f" ] && rm -f "$f"
        nap 0.1
    done
    nap 4

    say "big drop: one 20k-line file, then gone"
    big="$dir/assets/huge_$RANDOM.txt"
    lines 20000 >"$big"
    nap 5
    rm -f "$big"
    nap 3
done
