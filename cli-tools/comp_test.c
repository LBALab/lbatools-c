#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lbatools/compress.h>


static int
file_exists(char *fn)
{
    FILE *f;
    int ret;

    f = fopen(fn, "rb");
    ret = (f == NULL) ? 0 : 1;
    fclose(f);
    return ret;
}


int
main(int argc, char *argv[])
{
    int dir, type, in_len, out_len;
    FILE *f;
    char *in, *out;

    printf("LBA Compression Test Program\n\n");

    if (argc != 5) {
	printf("Usage: Compress D N FILENAME.EXT FILENAME.EXT\n\n");
	printf("D: C = Compress, D = Decompress\n");
	printf("N: 1 = LZSS, 2 = LZMIT\n");
    } else {
	if (!stricmp(argv[1], "C"))
		dir = 0;	/* Compress. */
	else if (!stricmp(argv[1], "D"))
		dir = 1;	/* Decompress. */
	else {
		printf("Invalid direction: %s\n", argv[1]);
		return 1;
	}

	type = atoi(argv[2]);

	if ((type < 1) || (type > 2)) {
		printf("Invalid %scompression type: %s\n", dir ? "de" : "", argv[2]);
		return 2;
	} else if (type == 0)
		printf("%s using Store\n", dir ? "Decompressing" : "Compressing");
	else if (type == 1)
		printf("%s using LZSS\n", dir ? "Decompressing" : "Compressing");
	else
		printf("%s using LZMIT\n", dir ? "Decompressing" : "Compressing");

	if (!file_exists(argv[3])) {
		printf("File does not exist: %s\n", argv[3]);
		return 3;
	}

	if (file_exists(argv[4])) {
		printf("File already exists: %s\n", argv[4]);
		remove(argv[4]);
	}

	f = fopen(argv[3], "rb");
	fseek(f, 0, SEEK_END);
	in_len = ftell(f);
	in = (char *) malloc(in_len);
	fseek(f, 0, SEEK_SET);
	fread(in, 1, in_len, f);
	fclose(f);

	out = (char *) malloc(in_len << 1);

	if (dir)
		out_len = decompress(type, out, in, in_len);
	else
		out_len = compress(type, out, in, in_len);

	f = fopen(argv[4], "wb");
	fwrite(out, 1, out_len, f);
	fclose(f);

	free(out);
	free(in);

	printf("%s\n", dir ? "Decompressed:" : "Compressed:");
	printf("    Source file: %s (%i bytes)\n", argv[3], in_len);
	printf("    Destination file: %s (%i bytes)\n", argv[4], out_len);
    }

    return 0;
}