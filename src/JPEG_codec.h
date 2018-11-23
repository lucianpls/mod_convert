#if !defined(JPEG_CODEC_H)
#define JPEG_CODEC_H

#include "BitMask2D.h"
#include <setjmp.h>

// Could be used for short int, so make it a template
template<typename T> int apply_mask(BitMap2D<> *bm, T *ps, int nc = 3, int line_stride = 0) {
    int w = bm->getWidth();
    int h = bm->getHeight();

    // line_stride of zero means packed buffer
    if (line_stride == 0)
        line_stride = w * nc;
    else
        line_stride /= sizeof(T); // Convert from bytes to type stride

    // Count the corrections
    int count = 0;
    for (int y = 0; y < h; y++) {
        T *s = ps + y * line_stride;
        for (int x = 0; x < w; x++) {
            if (bm->isSet(x, y)) { // Should be non-zero
                for (int c = 0; c < nc; c++, s++) {
                    if (*s == 0) {
                        *s = 1;
                        count++;
                    }
                }
            }
            else { // Should be zero
                for (int c = 0; c < nc; c++, s++) {
                    if (*s != 0) {
                        *s = 0;
                        count++;
                    }
                }
            }
        }
    }
    return count;
}

#endif
