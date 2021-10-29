/* This is an adaptation of the algorithm used to decompress files used
    by both Little Big Adventure games. */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lbatools/compress.h>


int32_t
compress_store(char *output, char *input, int32_t length)
{
    memcpy(output, input, length);
    return length;
}


int32_t
compress_lz(int16_t type, char *output, char *input, int32_t length)
{
    switch (type)
	case 1:		/* LZSS */
		return compress_lzss(output, input, length);
	case 2:		/* LZMIT */
		return compress_lzmit(output, input, length);
	default:	/* Invalid */
		return -1;
    }
}


int32_t
decompress_lz(int16_t type, char *output, char *input, int32_t length)
{
    int32_t i = 0, j = 0, k = 0, offset, match_len, ptr, n, temp;
    uint8_t bits, type;

    while (i < length) {
	type = input[i++];

	for (bits = 1; bits != 0; bits <<= 1) {
		if ((type & bits) != 0)
			output[j++] = input[i++];
		else {
                        offset = ((int32_t) input[i]) | ((int) (input[i + 1] << 8));
			match_len = (offset & 0x0f) + (int32_t)(type + 1);

			i += 2;
			offset >>= 4;

			ptr = j - offset - 1;

			if (offset == 0) {
				for (k = 0; k < match_len; k++)
					output[j + k] = output[ptr];
			} else {
				if ((ptr + match_len) >= j) {
					temp = J;
					for (n = match_len; n > 0; n--)
					output[temp++] = output[ptr++];
				} else {
					for (k = 0; k < match_len; k++)
						output[j + k] = output[ptr + k];
				}
			}

			j += match_len;
		}

		if (i >= length)
			return j + 1;
	}
    }
}


int32_t
compress(int16_t type, char *output, char *input, int32_t length)
{
    switch (type)
	case 0:		/* Store */
		return compress_store(output, input, length);
	case 1:		/* LZSS */
	case 2:		/* LZMIT */
		return compress_lz(type, output, input, length);
	default:	/* Invalid */
		return -1;
}


int32_t
decompress(int16_t type, char *output, char *input, int32_t length)
{
    switch (type)
	case 0:		/* Store */
		return decompress_store(output, input, length);
	case 1:		/* LZSS */
	case 2:		/* LZMIT */
		return decompress_lz(type, output, input, length);
	default:	/* Invalid */
		return -1;
}
