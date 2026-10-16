//-----------------------------------------------------------------------------
// This file is part of AEO-Light
//
// Copyright (c) 2016-2025 University of South Carolina
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version.
//
// AEO-Light is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//
// Funding for AEO-Light development was provided through a grant from the
// National Endowment for the Humanities
//-----------------------------------------------------------------------------

#include "readframejpg.h"

#include <cstring>

#include <QImage>
#include <QImageReader>

#include "aeoexception.h"

double *ReadFrameJPG(const char *fn, double *buf)
{
	QImageReader reader(QString::fromUtf8(fn));
	reader.setAutoTransform(true);
	QImage img = reader.read();

	if(img.isNull())
	{
		QString msg("ReadFrameJPG: Cannot open ");
		msg += fn;
		if(!reader.errorString().isEmpty())
		{
			msg += "\n";
			msg += reader.errorString();
		}
		throw AeoException(msg);
	}

	QImage rgb = img.convertToFormat(QImage::Format_RGB888);
	int width = rgb.width();
	int height = rgb.height();

	if(buf == NULL)
	{
		buf = new double[width * height];
		if(buf == NULL)
			throw AeoException("Out of memory: JPG frame buffer");
	}

	long i(0);
	double scale = 1.0 / (3.0 * 255.0);
	for(int y = 0; y < height; ++y)
	{
		const unsigned char *row = rgb.constScanLine(y);
		for(int x = 0; x < width; ++x, ++i)
		{
			buf[i] = (double(row[3*x]) +
					double(row[3*x+1]) +
					double(row[3*x+2])) * scale;
		}
	}

	return buf;
}

unsigned char *ReadFrameJPG_ImageData(const char *fn, unsigned char *buf,
		int &width, int &height, bool &endian,
		GLenum &pix_fmt, int &num_components)
{
	QImageReader reader(QString::fromUtf8(fn));
	reader.setAutoTransform(true);
	QImage img = reader.read();

	if(img.isNull())
	{
		QString msg("ReadFrameJPG: Cannot open ");
		msg += fn;
		if(!reader.errorString().isEmpty())
		{
			msg += "\n";
			msg += reader.errorString();
		}
		throw AeoException(msg);
	}

	QImage rgb = img.convertToFormat(QImage::Format_RGB888);
	width = rgb.width();
	height = rgb.height();

	num_components = 3;
	pix_fmt = GL_UNSIGNED_BYTE;
	endian = false;

	if(buf == NULL)
	{
		buf = new unsigned char[width * height * num_components];
		if(buf == NULL)
			throw AeoException("Out of memory: JPG image buffer");
	}

	for(int y = 0; y < height; ++y)
	{
		memcpy(buf + y * width * num_components, rgb.constScanLine(y),
				width * num_components);
	}

	return buf;
}
