/*
 * cdxl.datatype's core on the host (tests/test-cdxl.sh): walks a CDXL film
 * frame by frame as the datatype does, and writes what each frame shows
 * (NAME.got.rgb) and its sound as 8-bit mono (NAME.got.pcm), with one line
 * a film: NAME width height frames fps rate. A film cut short or with a bad
 * header plays the frames before it, as the datatype does.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdxl_core.h"

static int play(const char *path, const char *name)
{
    FILE *f = fopen(path, "rb"), *rgb, *pcm;
    char out[512];
    uint8_t head[CDXL_HEAD], *frame, *pix;
    struct cdxl_frame first, fr;
    unsigned frames = 0;

    if (!f)
        return 1;
    snprintf(out, sizeof out, "%s.got.rgb", name);
    rgb = fopen(out, "wb");
    snprintf(out, sizeof out, "%s.got.pcm", name);
    pcm = fopen(out, "wb");
    memset(&first, 0, sizeof first);
    for (;;) {
        long at = ftell(f);
        if (fread(head, 1, CDXL_HEAD, f) != CDXL_HEAD || cdxl_parse(head, &fr))
            break;
        if (!(frame = malloc(fr.size)))
            break;
        fseek(f, at, SEEK_SET);
        if (fread(frame, 1, fr.size, f) != fr.size) {
            free(frame);
            break;
        }
        if (!frames)
            first = fr;
        if (!(pix = malloc((size_t)fr.width * fr.height * 3))) {
            free(frame);
            break;
        }
        cdxl_show(frame, &fr, pix);
        fwrite(pix, 1, (size_t)fr.width * fr.height * 3, rgb);
        if (fr.layout == CDXL_CHUNKY) {          /* the datatype's 256 colours: every index in the cube */
            unsigned i;
            cdxl_rgb_to_cube(frame, &fr, pix);
            for (i = 0; i < (unsigned)fr.width * fr.height; i++)
                if (pix[i] >= 216)
                    return 2;
        }
        if (fr.audio) {
            int8_t *mono = malloc(fr.audio);
            if (mono) {
                cdxl_mono(frame, &fr, mono);
                fwrite(mono, 1, fr.audio, pcm);
                free(mono);
            }
        }
        free(pix);
        free(frame);
        frames++;
    }
    fclose(f);
    fclose(rgb);
    fclose(pcm);
    printf("%s %u %u %u %u %u\n", name, first.width, first.height, frames, frames ? cdxl_fps(&first) : 0,
           frames ? cdxl_rate(&first) : 0);
    return 0;
}

int main(int argc, char **argv)
{
    int i, bad = 0;

    for (i = 1; i + 1 < argc; i += 2)
        bad |= play(argv[i], argv[i + 1]);
    return bad;
}
