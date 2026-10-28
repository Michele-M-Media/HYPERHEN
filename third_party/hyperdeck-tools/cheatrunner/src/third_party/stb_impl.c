/* Single translation unit that compiles the stb_image and stb_image_write
   implementations. Public domain. */

#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wcomment"

#define STB_IMAGE_IMPLEMENTATION
/* JPEG, PNG, BMP, TGA — everything common a user might drop in via FTP.
   Skip HDR/linear because we never need them and they pull in extra code. */
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
