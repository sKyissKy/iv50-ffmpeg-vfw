/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "iv50_vfw.h"

#include <stdlib.h>

struct Iv50Decoder { int begun; };

Iv50Decoder *iv50_decoder_create(void)
{
    return (Iv50Decoder *)calloc(1, sizeof(Iv50Decoder));
}

void iv50_decoder_destroy(Iv50Decoder *decoder)
{
    free(decoder);
}

int iv50_decoder_begin(
    Iv50Decoder *decoder,
    const BITMAPINFOHEADER *input,
    const BITMAPINFOHEADER *output)
{
    if (decoder == NULL ||
        !iv50_is_output_format_supported(input, output)) {
        return ICERR_BADFORMAT;
    }
    decoder->begun = 1;
    return ICERR_OK;
}

int iv50_decoder_decode(Iv50Decoder *decoder, const ICDECOMPRESS *request)
{
    return decoder != NULL && decoder->begun && request != NULL
        ? ICERR_OK
        : ICERR_BADPARAM;
}

void iv50_decoder_end(Iv50Decoder *decoder)
{
    if (decoder != NULL) {
        decoder->begun = 0;
    }
}
