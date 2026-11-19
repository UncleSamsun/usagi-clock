#!/usr/bin/env bash
# 테스트용 MP3를 ffmpeg로 만든다(직접 만든 사인파라 저작물 문제가 없다). 만든 파일은 저장소에 커밋한다.
#   준비: sudo apt install -y ffmpeg
#   실행: test/host/fixtures/make_fixtures.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=440:duration=1" -ar 44100 -ac 1 -b:a 64k tone440_44k_mono.mp3
ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=440:duration=1" -ar 44100 -ac 2 -b:a 128k \
    -metadata title=usagi-test -id3v2_version 3 tone440_44k_stereo_id3.mp3
ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=1000:duration=2" -ar 22050 -ac 1 -b:a 32k tone1k_22k_mono_2s.mp3

ls -l ./*.mp3
