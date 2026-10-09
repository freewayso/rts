#!/usr/bin/env bash
# Clean dated logs under /data/n1server/log older than N days.
# Prefer filename date (YYYY-MM-DD); fall back to mtime for undated files.
#
# Usage:
#   ./clean_n1server_logs.sh              # dry-run
#   ./clean_n1server_logs.sh --apply      # delete
#
# Cron example (daily 03:10):
#   10 3 * * * /data/n1server/scripts/clean_n1server_logs.sh --apply >> /var/log/clean_n1server_logs.log 2>&1

set -euo pipefail

LOG_DIR="${LOG_DIR:-/data/n1server/log}"
KEEP_DAYS="${KEEP_DAYS:-1}"   # keep today + (KEEP_DAYS-1) previous calendar days? 
# KEEP_DAYS=1 => keep only files whose name date is today (delete yesterday and older)
APPLY=0

for arg in "$@"; do
  case "$arg" in
    --apply|-f|--force) APPLY=1 ;;
    --help|-h)
      cat <<EOF
Usage: $0 [--apply]
  Keep logs whose filename date is within the last ${KEEP_DAYS} day(s) (including today).
  Also delete undated files whose mtime is older than ${KEEP_DAYS} day(s).
  Default is dry-run; pass --apply to delete.
Env:
  LOG_DIR=$LOG_DIR
  KEEP_DAYS=$KEEP_DAYS
EOF
      exit 0
      ;;
    *)
      echo "Unknown arg: $arg" >&2
      exit 1
      ;;
  esac
done

if [[ ! -d "$LOG_DIR" ]]; then
  echo "ERROR: log dir not found: $LOG_DIR" >&2
  exit 1
fi

# Cutoff date string YYYY-MM-DD: files with name date < cutoff are deleted.
# KEEP_DAYS=1 => cutoff = today (delete anything dated before today).
CUTOFF="$(date -d "-$((KEEP_DAYS - 1)) days" '+%Y-%m-%d' 2>/dev/null \
  || date -v-"$((KEEP_DAYS - 1))"d '+%Y-%m-%d')"

echo "[$(date '+%F %T')] dir=$LOG_DIR keep_days=$KEEP_DAYS cutoff_date>=$CUTOFF apply=$APPLY"

deleted=0
kept=0
skipped=0

while IFS= read -r -d '' f; do
  base="$(basename -- "$f")"

  # Match first YYYY-MM-DD in filename
  if [[ "$base" =~ ([0-9]{4}-[0-9]{2}-[0-9]{2}) ]]; then
    fdate="${BASH_REMATCH[1]}"
    if [[ "$fdate" < "$CUTOFF" ]]; then
      if [[ "$APPLY" -eq 1 ]]; then
        rm -f -- "$f"
        echo "DELETED (by-name $fdate) $f"
      else
        echo "WOULD_DELETE (by-name $fdate) $f"
      fi
      deleted=$((deleted + 1))
    else
      echo "KEEP (by-name $fdate) $f"
      kept=$((kept + 1))
    fi
    continue
  fi

  # Undated files (e.g. *_WarnError*.log): use mtime
  # -mtime +0 with KEEP_DAYS=1 => older than 24h roughly; use day count
  age_days=$((KEEP_DAYS))
  if find "$f" -mtime "+$((age_days - 1))" | grep -q .; then
    if [[ "$APPLY" -eq 1 ]]; then
      rm -f -- "$f"
      echo "DELETED (by-mtime) $f"
    else
      echo "WOULD_DELETE (by-mtime) $f"
    fi
    deleted=$((deleted + 1))
  else
    echo "KEEP (undated/recent) $f"
    skipped=$((skipped + 1))
  fi
done < <(find "$LOG_DIR" -type f -print0 | sort -z)

echo "summary: delete=$deleted keep=$kept undated_keep=$skipped"

if [[ "$APPLY" -eq 0 ]]; then
  echo "Dry-run only. Re-run with --apply to delete."
fi
