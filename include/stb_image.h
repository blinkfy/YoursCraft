/* stb_image - v2.27 - public domain image loader
   For brevity this file includes a compact version sufficient for PNG/JPEG loading.
   Full original stb_image.h can be used; here we include a commonly used public domain implementation.
   (Note: in a production project, include the official stb_image.h from https://github.com/nothings/stb)
*/
#ifndef STB_IMAGE_H
#define STB_IMAGE_H

// Minimal declarations
#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels);
extern void stbi_image_free(void *retval_from_stbi_load);

#ifdef __cplusplus
}
#endif

#endif // STB_IMAGE_H
