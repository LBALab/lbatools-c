#ifndef LBATOOLS_COMPRESS_H
# define LBATOOLS_COMPRESS_H


extern int32_t	compress(int16_t type, char *output, char *input, int32_t length);
extern int32_t	compress_lz(int16_t type, char *output, char *input, int32_t length);
extern int32_t	compress_store(char *output, char *input, int32_t length);
extern int32_t	compress_lzss(char *output, char *input, int32_t length);
extern int32_t	compress_lzmit(char *output, char *input, int32_t length);

extern int32_t	decompress(int16_t type, char *output, char *input, int32_t length);
extern int32_t	decompress_lz(int16_t type, char *output, char *input, int32_t length);


#define decompress_store	compress_storef


#endif	/*LBATOOLS_COMPRESS_H*/
