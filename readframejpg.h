#ifndef READFRAMEJPG_H
#define READFRAMEJPG_H

#include <QOpenGLTexture>

double *ReadFrameJPG(const char *fn, double *buf);
unsigned char *ReadFrameJPG_ImageData(const char *fn, unsigned char *buf,
		int &width, int &height, bool &endian,
		GLenum &pix_fmt, int &num_components);

#endif // READFRAMEJPG_H
