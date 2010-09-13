/*
 * Copyright (c) 2010 Aleksey Yeschenko <aleksey@yeschenko.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "erl_nif.h"

static const struct { int gsm_ext; int code_point; } gsm_exts[] = {
    {0x0A, 0x000C}, {0x14, 0x005E}, {0x28, 0x007B}, {0x29, 0x007D}, {0x2F, 0x005C},
    {0x3C, 0x005B}, {0x3D, 0x007E}, {0x3E, 0x005D}, {0x40, 0x007C}, {0x65, 0x20AC}
};

static const int gsm_to_code_point[128] = {
    0x0040, 0x00A3, 0x0024, 0x00A5, 0x00E8, 0x00E9, 0x00F9, 0x00EC,
    0x00F2, 0x00E7, 0x000A, 0x00D8, 0x00F8, 0x000D, 0x00C5, 0x00E5,
    0x0394, 0x005F, 0x03A6, 0x0393, 0x039B, 0x03A9, 0x03A0, 0x03A8,
    0x03A3, 0x0398, 0x039E, 0x00A0, 0x00C6, 0x00E6, 0x00DF, 0x00C9,
    ' ',    '!',    '"',    '#',    0x00A4, '%',    '&',    '\'',
    '(',    ')',    '*',    '+',    ',',    '-',    '.',    '/',
    '0',    '1',    '2',    '3',    '4',    '5',    '6',    '7',
    '8',    '9',    ':',    ';',    '<',    '=',    '>',    '?',
    0x00A1, 'A',    'B',    'C',    'D',    'E',    'F',    'G',
    'H',    'I',    'J',    'K',    'L',    'M',    'N',    'O',
    'P',    'Q',    'R',    'S',    'T',    'U',    'V',    'W',
    'X',    'Y',    'Z',    0x00C4, 0x00D6, 0x00D1, 0x00DC, 0x00A7,
    0x00BF, 'a',    'b',    'c',    'd',    'e',    'f',    'g',
    'h',    'i',    'j',    'k',    'l',    'm',    'n',    'o',
    'p',    'q',    'r',    's',    't',    'u',    'v',    'w',
    'x',    'y',    'z',    0x00E4, 0x00F6, 0x00F1, 0x00FC, 0x00E0
};

#define VALID(bin)   enif_make_tuple2(env, gsm0338_atoms.valid,   enif_make_binary(env, bin))
#define INVALID(bin) enif_make_tuple2(env, gsm0338_atoms.invalid, enif_make_binary(env, bin))
#define ENOMEMERROR  enif_make_tuple2(env, gsm0338_atoms.error,   gsm0338_atoms.enomem)

static struct {
    ERL_NIF_TERM valid;
    ERL_NIF_TERM invalid;
    ERL_NIF_TERM error;
    ERL_NIF_TERM enomem;
} gsm0338_atoms;

static int
load(ErlNifEnv* env, void** priv, ERL_NIF_TERM load_info)
{
    gsm0338_atoms.valid   = enif_make_atom(env, "valid");
    gsm0338_atoms.invalid = enif_make_atom(env, "invalid");
    gsm0338_atoms.error   = enif_make_atom(env, "error");
    gsm0338_atoms.enomem  = enif_make_atom(env, "enomem");

    return 0;
}

static ERL_NIF_TERM
gsm0338_from_utf8(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary utf_bin, gsm_bin;
    int pos, len;
    int outpos = 0, outlen;
    int b1, b2, b3, b4, cp;
    int gsm, extended;
    int i;
    int valid = 1;

    if (!enif_inspect_binary(env, argv[0], &utf_bin)) {
        return enif_make_badarg(env);
    }

    len = utf_bin.size;

    if (len == 0) {
        return VALID(&utf_bin);
    }

    outlen = utf_bin.size;

    if (!enif_alloc_binary(outlen, &gsm_bin)) {
        return ENOMEMERROR;
    }

    for (pos = 0; pos < len; pos++) {
        b1 = utf_bin.data[pos];

        if ((b1 >= 0x80 && b1 <= 0xC1) || b1 >= 0xF5) {
            valid = 0;
            continue;
        }

        if (b1 <= 127) {
            /* single byte */
            cp = b1;
        } else if ((b1 & 0xE0) == 0xC0) {
            /* two bytes */
            if (len < pos + 2) {
                valid = 0;
                continue;
            }

            b2 = utf_bin.data[pos + 1];

            if ((b2 & 0xC0) != 0x80) {
                /* unexpected second byte */
                valid = 0;
                continue;
            }

            cp = ((b1 & 0x1F) << 6) | (b2 & 0x3F);
            pos += 1;
        } else if ((b1 & 0xF0) == 0xE0) {
            /* three bytes */
            if (len < pos + 3) {
                valid = 0;
                continue;
            }

            b2 = utf_bin.data[pos + 1];
            b3 = utf_bin.data[pos + 2];

            if (((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80)) {
                /* unexpected second or third byte */
                valid = 0;
                continue;
            }

            cp = ((b1 & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            pos += 2;
        } else if ((b1 & 0xF8) == 0xF0) {
            /* four bytes */
            if (len < pos + 4) {
                valid = 0;
                continue;
            }

            b2 = utf_bin.data[pos + 1];
            b3 = utf_bin.data[pos + 2];
            b4 = utf_bin.data[pos + 3];

            if (((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80) || ((b4 & 0xC0) != 0x80)) {
                /* unexpected second, third or fourth byte */
                valid = 0;
                continue;
            }

            cp = ((b1 & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
            pos += 3;
        }

        /* convert the codepoint to gsm */
        
        gsm = -1;
        extended = 0;

        if ((cp >= 32 && cp <= 35) ||
              (cp >= 37 && cp <= 63) ||
              (cp >= 65 && cp <= 90) ||
              (cp >= 97 && cp <= 122)) {
            gsm = cp;
        }

        if (gsm == -1) {
            for (i = 0; i < 128; i++) {
                if (gsm_to_code_point[i] == cp) {
                    gsm = i;
                    break;
                }
            }
        }

        if (gsm == -1) {
            for (i = 0; i < 10; i++) {
                if (gsm_exts[i].code_point == cp) {
                    gsm = gsm_exts[i].gsm_ext;
                    extended = 1;
                    break;
                }
            }
        }

        if (gsm == -1)
            gsm = 0x3F; /* '?' */

        if (outlen < outpos + extended + 1) {
            outlen = outpos + extended + 1;
            if (!enif_realloc_binary(&gsm_bin, outlen)) {
                enif_release_binary(&gsm_bin);
                return ENOMEMERROR;
            }
        }

        if (extended)
            gsm_bin.data[outpos++] = 0x1B;

        gsm_bin.data[outpos++] = gsm;
    }

    /* trim */
    if (outpos < outlen)
        enif_realloc_binary(&gsm_bin, outpos);

    if (valid) { 
        return VALID(&gsm_bin);
    } else {
        return INVALID(&gsm_bin);
    }
}

static ERL_NIF_TERM
gsm0338_to_utf8(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary gsm_bin, utf_bin;
    int pos, len;
    int outpos = 0, outlen;
    int valid = 1;
    int c, i;

    if (!enif_inspect_binary(env, argv[0], &gsm_bin)) {
        return enif_make_badarg(env);
    }

    len = gsm_bin.size;

    if (len == 0) {
        return VALID(&gsm_bin);
    }

    outlen = gsm_bin.size;

    if (!enif_alloc_binary(outlen, &utf_bin)) {
        return ENOMEMERROR;
    }

    for (pos = 0; pos < len; pos++) {
        c = gsm_bin.data[pos];

        if (c > 127) {
            valid = 0;
            continue;
        }

        if (c == 27 && pos + 1 == len) {
            valid = 0;
            continue;
        }

        if (c == 27) {
            pos++;
            c = 0x00A0; /* NBSP */

            for (i = 0; i < 10; i++) {
                if (gsm_exts[i].gsm_ext == gsm_bin.data[pos]) {
                    c = gsm_exts[i].code_point;
                    break;
                }
            }

            if (c == 0x00A0)
                valid = 0;
        } else {
            c = gsm_to_code_point[c];
        }

        /* encode the code point */
        if (c <= 127) {
            /* single byte */
            if (outlen < outpos + 1) {
                outlen += 1;
                if (!enif_realloc_binary(&utf_bin, outlen)) {
                    enif_release_binary(&utf_bin);
                    return ENOMEMERROR;
                }
            }
            utf_bin.data[outpos++] = c;
        } else if (c != 0x20AC) {
            /* two bytes */
            if (outlen < outpos + 2) {
                outlen = outpos + 2;
                if (!enif_realloc_binary(&utf_bin, outlen)) {
                    enif_release_binary(&utf_bin);
                    return ENOMEMERROR;
                }
            }
            utf_bin.data[outpos++] = ((c >> 6 | 0xC0) & 0xFF);
            utf_bin.data[outpos++] = (c & 0x3F) | 0x80;
        } else {
            /* euro sign */
            if (outlen < outpos + 3) {
                outlen = outpos + 3;
                if (!enif_realloc_binary(&utf_bin, outlen)) {
                    enif_release_binary(&utf_bin);
                    return ENOMEMERROR;
                }
            }
            utf_bin.data[outpos++] = 0xE2;
            utf_bin.data[outpos++] = 0x82;
            utf_bin.data[outpos++] = 0xAC;
        }
    }

    /* trim */
    if (outpos < outlen)
        enif_realloc_binary(&utf_bin, outpos);

    if (valid) { 
        return VALID(&utf_bin);
    } else {
        return INVALID(&utf_bin);
    }
}

static ErlNifFunc nif_funcs[] = {
    {"from_utf8", 1, gsm0338_from_utf8},
    {"to_utf8",   1, gsm0338_to_utf8}
};

ERL_NIF_INIT(gsm0338, nif_funcs, load, NULL, NULL, NULL)
