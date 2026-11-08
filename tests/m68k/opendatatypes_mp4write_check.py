#!/usr/bin/env python3
"""The outside checks on the MP4s opendatatypes_mp4write_test.c wrote
(tests/m68k/test_opendatatypes_mp4write.sh runs it):

- FFprobe reads each file: one H.264 Baseline stream of the film's size,
  every frame, its frame rate; 8-bit PCM of the right rate and channels
  when the film has sound, else no sound stream.
- FFmpeg decodes every frame: luma PSNR against what was encoded (NAME.yuv),
  and RGB PSNR against what was encoded shown as RGB the same way, at least
  the thresholds below; its sound is the film's, byte for byte (NAME.pcm).
  (The RGB PSNR against the film's own colours, NAME.rgb, is printed too:
  4:2:0 halves the colour, so a palette's saturated neighbours lose most
  there whatever the quality.)
- FFmpeg's IFF ANIM decoder shows the same frames written as an ANIM
  exactly as this test reads the planes (NAME.anim against NAME.rgb): HAM,
  EHB and the colours are what an outside reader takes them for.
- The encoder built on the host (MINIH264_NO_SIMD, the same plain C) makes
  from NAME.yuv the very NAL units the m68k build put in the MP4, and the
  same SPS and PPS as its avcC.

    python3 opendatatypes_mp4write_check.py DIR [HOST_ENCODER]

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import json
import math
import os
import shutil
import struct
import subprocess
import sys

# Luma PSNR (dB) against the encoded YUV: the worst frame and the film's
# mean squared error, by quality. Noise has no structure to keep: its own.
Y_MIN = {10: 40.0, 26: 30.0, 51: 14.0}
Y_MIN_NOISE = 20.0
# RGB PSNR against what was encoded, both shown as RGB by FFmpeg
RGB_MIN = 26.0
RGB_MIN_NOISE = 20.0

fails = 0
checks = 0


def check(ok, what, name):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print('FAIL %s: %s' % (name, what))


def run(args, binary=True):
    r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode:
        return None
    return r.stdout if binary else r.stdout.decode()


def psnr(a, b):
    if len(a) != len(b) or not a:
        return -1.0
    se = sum((x - y) * (x - y) for x, y in zip(a, b))
    return 99.0 if se == 0 else 10 * math.log10(255.0 * 255.0 * len(a) / se)


def boxes(d, at, end):
    while at + 8 <= end:
        n, t = struct.unpack('>I4s', d[at:at + 8])
        if n < 8 or at + n > end:
            return
        yield t.decode('latin-1'), at + 8, at + n
        at += n


def box(d, at, end, path):
    for name in path:
        for t, s, e in boxes(d, at, end):
            if t == name:
                at, end = s, e
                break
        else:
            return None
    return at, end


def mp4_nals(d):
    """the video track's samples as lists of NAL units, and its avcC's SPS and PPS"""
    moov = box(d, 0, len(d), ['moov'])
    trak = next((s, e) for t, s, e in boxes(d, moov[0], moov[1]) if t == 'trak')
    stbl = box(d, trak[0], trak[1], ['mdia', 'minf', 'stbl'])
    stsd = box(d, stbl[0], stbl[1], ['stsd'])
    avcc = box(d, stsd[0] + 8 + 86, stsd[1], ['avcC'])
    c = d[avcc[0]:avcc[1]]
    sl = struct.unpack('>H', c[6:8])[0]
    sps = c[8:8 + sl]
    pl = struct.unpack('>H', c[9 + sl:11 + sl])[0]
    pps = c[11 + sl:11 + sl + pl]
    sz = box(d, stbl[0], stbl[1], ['stsz'])
    co = box(d, stbl[0], stbl[1], ['stco'])
    n = struct.unpack('>I', d[sz[0] + 8:sz[0] + 12])[0]
    out = []
    for i in range(n):
        size = struct.unpack('>I', d[sz[0] + 12 + 4 * i:sz[0] + 16 + 4 * i])[0]
        at = struct.unpack('>I', d[co[0] + 8 + 4 * i:co[0] + 12 + 4 * i])[0]
        k, nals = 0, []
        while k < size:
            ln = struct.unpack('>I', d[at + k:at + k + 4])[0]
            nals.append(d[at + k + 4:at + k + 4 + ln])
            k += 4 + ln
        out.append(nals)
    return out, sps, pps


def annexb_nals(d):
    out, i = [], 0
    starts = []
    while True:
        j = d.find(b'\x00\x00\x00\x01', i)
        if j < 0:
            break
        starts.append(j)
        i = j + 4
    for k, s in enumerate(starts):
        out.append(d[s + 4:starts[k + 1] if k + 1 < len(starts) else len(d)])
    return out


def main():
    base = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else None
    ff = shutil.which('ffmpeg')
    fp = shutil.which('ffprobe')
    if not ff or not fp:
        print('ffmpeg: not installed, its checks skipped')
    worst = {}
    for line in open(os.path.join(base, 'films.txt')):
        f = line.split()
        if f[0].startswith('#'):
            # written through the class: FFprobe and FFmpeg read it
            name = f[0][1:]
            if fp:
                p = os.path.join(base, name + '.mp4')
                j = run([fp, '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', p], False)
                st = json.loads(j)['streams'] if j else []
                v = [s for s in st if s['codec_type'] == 'video']
                a = [s for s in st if s['codec_type'] == 'audio']
                check(len(v) == 1 and v[0]['codec_name'] == 'h264' and v[0]['nb_read_frames'] == f[3] and
                      len(a) == 1 and a[0]['codec_name'] == 'pcm_s8', 'ffprobe reads it', name)
            continue
        name = f[0]
        w, h, frames, fps, ticks, qp, speed, gop, channels, rate = map(int, f[1:11])
        cw, ch = (w + 1) & ~1, (h + 1) & ~1
        p = os.path.join(base, name + '.mp4')
        data = open(p, 'rb').read()
        yuv = open(os.path.join(base, name + '.yuv'), 'rb').read()
        rgb = open(os.path.join(base, name + '.rgb'), 'rb').read()
        noise = name.startswith('noise')
        if fp:
            j = run([fp, '-v', 'error', '-count_frames', '-show_streams', '-show_format', '-of', 'json', p], False)
            check(j is not None, 'ffprobe reads it', name)
            if j:
                info = json.loads(j)
                st = info['streams']
                v = [s for s in st if s['codec_type'] == 'video']
                a = [s for s in st if s['codec_type'] == 'audio']
                check(len(v) == 1 and v[0]['codec_name'] == 'h264' and v[0].get('profile') == 'Baseline',
                      'one H.264 Baseline stream', name)
                if v:
                    v = v[0]
                    check(v['width'] == cw and v['height'] == ch, 'its size %sx%s' % (v['width'], v['height']), name)
                    check(int(v['nb_read_frames']) == frames, 'every frame (%s)' % v['nb_read_frames'], name)
                    num, den = map(int, v['avg_frame_rate'].split('/'))
                    check(den and abs(num / den - frames * fps / ticks) < 0.01,
                          'frame rate %s' % v['avg_frame_rate'], name)
                if channels:
                    check(len(a) == 1 and a[0]['codec_name'] == 'pcm_s8' and int(a[0]['sample_rate']) == rate and
                          int(a[0]['channels']) == channels, 'its sound: 8-bit PCM, rate, channels', name)
                else:
                    check(not a, 'no sound stream', name)
        if ff:
            dec = run([ff, '-v', 'error', '-i', p, '-map', '0:v', '-fps_mode', 'passthrough', '-f', 'rawvideo',
                       '-pix_fmt', 'yuv420p', '-'])
            fs = cw * ch * 3 // 2
            check(dec is not None and len(dec) == len(yuv), 'FFmpeg decodes every frame', name)
            if dec and len(dec) == len(yuv):
                ys = [psnr(dec[k * fs:k * fs + cw * ch], yuv[k * fs:k * fs + cw * ch]) for k in range(frames)]
                lim = Y_MIN_NOISE if noise else Y_MIN.get(qp, 30.0 if qp <= 30 else 14.0)
                check(min(ys) >= lim, 'luma PSNR %.2f dB (worst frame) under %.1f' % (min(ys), lim), name)
                worst.setdefault(qp, []).append((min(ys), name))
            vf = 'scale=in_range=tv:out_range=pc:flags=accurate_rnd+full_chroma_int,crop=%d:%d:0:0' % (w, h)
            dec = run([ff, '-v', 'error', '-i', p, '-map', '0:v', '-fps_mode', 'passthrough', '-vf', vf,
                       '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
            enc = run([ff, '-v', 'error', '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-s', '%dx%d' % (cw, ch), '-i',
                       os.path.join(base, name + '.yuv'), '-vf', vf, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
            if dec and enc and len(dec) == len(rgb) == len(enc):
                r = psnr(dec, enc)
                lim = RGB_MIN_NOISE if noise else RGB_MIN
                check(qp > 30 or r >= lim, 'RGB PSNR %.2f dB under %.1f' % (r, lim), name)
                print('     %-44s Y %5.2f dB (worst frame)  RGB %5.2f dB (%5.2f against the film)  %6d bytes' %
                      (name, min(ys) if ys else -1, r, psnr(dec, rgb), len(data)))
            else:
                check(False, 'FFmpeg decodes it to RGB', name)
            an = os.path.join(base, name + '.anim')
            if os.path.exists(an):
                dec = run([ff, '-v', 'error', '-i', an, '-fps_mode', 'passthrough', '-f', 'rawvideo', '-pix_fmt',
                           'rgb24', '-'])
                check(dec == rgb, "FFmpeg's ANIM reader sees the same pixels as the test", name)
            if channels:
                dec = run([ff, '-v', 'error', '-i', p, '-map', '0:a', '-f', 's8', '-'])
                pcm = open(os.path.join(base, name + '.pcm'), 'rb').read()
                check(dec == pcm, 'the sound, byte for byte', name)
        if host:
            out = os.path.join(base, name + '.264')
            r = subprocess.run([host, str(cw), str(ch), str(qp), str(speed), str(gop),
                                os.path.join(base, name + '.yuv'), out])
            check(r.returncode == 0, 'the host encoder runs', name)
            if r.returncode == 0:
                hn = annexb_nals(open(out, 'rb').read())
                samples, sps, pps = mp4_nals(data)
                ps = [n for n in hn if n[0] & 31 in (7, 8)]
                rest = [n for n in hn if n[0] & 31 not in (7, 8)]
                check(ps[:2] == [sps, pps], "the host's SPS and PPS are the avcC's", name)
                check(rest == [n for s in samples for n in s],
                      "the host's NAL units are the m68k build's, byte for byte", name)
    print('%d checks, %d failed' % (checks, fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
