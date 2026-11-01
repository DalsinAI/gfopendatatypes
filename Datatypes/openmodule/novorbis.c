/*
 * gfopenmodule.datatype: libxmp without stb_vorbis. An XM whose samples are
 * Ogg Vorbis (OggMod, rare) does not load; the decoder is large and all
 * floating point, slow on a 68k without an FPU.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
int libxmp_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *rate, short **output);

int libxmp_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *rate, short **output)
{
    (void)mem;
    (void)len;
    (void)channels;
    (void)rate;
    (void)output;
    return -1;
}
