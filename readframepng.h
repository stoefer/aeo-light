#ifndef READFRAMEPNG_H
#define READFRAMEPNG_H

#include <QOpenGLTexture>

double *ReadFramePNG(const char *fn, double *buf);
unsigned char *ReadFramePNG_ImageData(const char *fn, unsigned char *buf,
		int &width, int &height, bool &endian,
		GLenum &pix_fmt, int &num_components);

#endif // READFRAMEPNG_H
