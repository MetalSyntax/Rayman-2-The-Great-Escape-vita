#!/bin/sh
# Builds ux0_data/rayman2thegreatescape/ (= ux0:data/rayman2thegreatescape/ on the PS Vita) from the
# decrypted app bundle (game_app -> Payload/Rayman2.app):
#   game_app/        bundle (executable + DATA/), no Apple signatures, no macOS ._* files
#                    DATA/WORLD/SOUNDIPHONE/*.BGM re-encoded in place AAC -> Ogg Opus (see below)
#   rebase.bin       rebase table (tools/macho_prep.py)
#   saves/           iOS Documents/ (deploy never overwrites it)
#   logs/            rayman2thegreatescape_001.log .. _999.log
#
# Why the BGM conversion: CBGMPlayer opens the .BGM music with AudioFileOpenURL; they are AAC in an
# MP4 container (m4af, 22050 Hz) and the loader has no AAC decoder. They become Ogg Opus 48 kHz
# stereo (libopusfile on the Vita, 48 kHz = sceAudioOut MAIN port rate). Only the staged copy
# changes; files already in Ogg ("OggS") are skipped, so re-running is incremental.
# There are no video files in this IPA (checked: no .m4v/.mp4/.mov/.avi; the CPA "Video" AI
# procedure fn_p_stVideo at Mach-O 0x40a34 is a stub that returns at once), so nothing to convert.
#
# Usage: tools/stage_data.sh [path/to/Rayman2.app]      (needs python3, rsync, ffmpeg with libopus)
set -e
cd "$(dirname "$0")/.."

APP="${1:-game_app}"
OUT="ux0_data/rayman2thegreatescape"
EXE=Rayman2

[ -f "$APP/$EXE" ] || { echo "[-] $APP/$EXE not found (game_app symlink to Payload/Rayman2.app?)"; exit 1; }
command -v ffmpeg >/dev/null || { echo "[-] ffmpeg not found (brew install ffmpeg)"; exit 1; }
ffmpeg -hide_banner -encoders 2>/dev/null | grep -q libopus || { echo "[-] ffmpeg without libopus"; exit 1; }

mkdir -p "$OUT/game_app" "$OUT/saves" "$OUT/logs" docs
echo "[*] Copying bundle -> $OUT/game_app/"
# Converted .BGM files must survive re-runs: they are excluded (and protected from --delete) here
# and copied below only when missing.
rsync -a --delete \
  --exclude '._*' --exclude '.DS_Store' --exclude '_CodeSignature' --exclude 'SC_Info' \
  --exclude 'embedded.mobileprovision' --exclude 'CodeResources' --exclude '*.opus.tmp' \
  --exclude '/DATA/WORLD/SOUNDIPHONE/*.BGM' \
  "$APP/" "$OUT/game_app/"
for src in "$APP"/DATA/WORLD/SOUNDIPHONE/*.BGM; do
  dst="$OUT/game_app/DATA/WORLD/SOUNDIPHONE/$(basename "$src")"
  [ -f "$dst" ] || cp "$src" "$dst"
done

echo "[*] BGM: AAC (m4af) -> Ogg Opus 48 kHz stereo"
conv=0; skip=0; fail=0
for f in "$OUT"/game_app/DATA/WORLD/SOUNDIPHONE/*.BGM; do
  if [ "$(head -c 4 "$f")" = "OggS" ]; then skip=$((skip + 1)); continue; fi
  if ffmpeg -nostdin -v error -y -i "$f" -vn -ac 2 -ar 48000 -c:a libopus -b:a 96k -f ogg "$f.opus.tmp"; then
    mv "$f.opus.tmp" "$f"; conv=$((conv + 1))
  else
    rm -f "$f.opus.tmp"; echo "    [!] failed: $f"; fail=$((fail + 1))
  fi
done
echo "    converted $conv, already Ogg $skip, failed $fail"
[ "$fail" = 0 ] || exit 1

echo "[*] rebase.bin"
python3 -I tools/macho_prep.py "$OUT/game_app/$EXE" "$OUT/rebase.bin" --report docs/macho_report.txt

# macOS writes AppleDouble ._* companions on exFAT for any file with xattrs: never ship them.
find "$OUT" -name '._*' -delete

# Self-checks: every BGM is Ogg, the executable is the decrypted one we analysed, no AppleDouble.
bad=$(for f in "$OUT"/game_app/DATA/WORLD/SOUNDIPHONE/*.BGM; do [ "$(head -c 4 "$f")" = "OggS" ] || echo "$f"; done)
[ -z "$bad" ] || { echo "[-] not Ogg: $bad"; exit 1; }
cmp -s "$APP/$EXE" "$OUT/game_app/$EXE" || { echo "[-] staged executable differs from $APP/$EXE"; exit 1; }
[ -z "$(find "$OUT" -name '._*' | head -1)" ] || { echo "[-] AppleDouble ._* files in $OUT"; exit 1; }
n_src=$(ls "$APP"/DATA/WORLD/SOUNDIPHONE/*.BGM | wc -l); n_dst=$(ls "$OUT"/game_app/DATA/WORLD/SOUNDIPHONE/*.BGM | wc -l)
[ "$n_src" = "$n_dst" ] || { echo "[-] BGM count $n_dst != $n_src"; exit 1; }

echo "[+] $OUT ready ($(du -sh "$OUT" | cut -f1)): game_app/, rebase.bin, saves/, logs/; $n_dst BGM in Ogg Opus"
echo "[+] Deploy: psvita-toolkit deploy --data   (or python3 -I tools/deploy_data.py)"
