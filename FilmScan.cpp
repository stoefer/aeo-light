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

// FilmScan -- objects for handling scanned images of film.
//
// FilmFrame - a scan of a single frame. Pixel values are mapped to [0-1], with
//             zero being black and one being white.
// FilmStrip - a sequence of FilmFrames (usually short; not the entire film)
// FilmScan - the main interface to a scanned film source (not used currently,
//            see project.h instead for an object that holds the working
//            project).
// SoundSignal - a sequence of values (doubles) for the sound extracted from
//               the film scan
//               each signal value is computed as the mean average of the
//               gray level of the pixels across the sound bounds area at
//               each scan row.
//

#define FILESCAN_CPP

#include <iostream>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <cmath>
#include <cstring>
#include <vector>

//#include <boost/numeric/ublas/matrix.hpp>

#include <QString>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QUuid>
#include <QStringList>

#ifdef Q_OS_WIN32
// Depending on the way you've built libtiff on Windows, you may have
// to adjust the way tiffio.h declares the libtiff functions.
// If you're getting errors about symbols starting with __imp_TIFF, like
//
// FilmScan.obj : error LNK2019: unresolved external symbol __imp_TIFFClose
//
// then define BUILD_LIBTIFF_DLL (which will make the linker look for
// the __exp_TIFFClose version instead)
//
// If you're getting other link errors about unresolved symbols related to
// libtiff, try defining one of these other constants:
// USE_LIBTIFF_DLL (look for __imp_TIFF)
// USE_LIBTIFF_STATIC (look for _TIFF)
//
  #define BUILD_LIBTIFF_DLL
#endif

#include <tiffio.h>

#ifdef Q_OS_WIN32
# ifdef BUILD_LIBTIFF_DLL
#  define LIBTIFF_DLL_IMPEXP     __DLL_EXPORT__
# elif defined(LIBTIFF_STATIC)
#  define LIBTIFF_DLL_IMPEXP
# elif defined (USE_LIBTIFF_DLL)
#  define LIBTIFF_DLL_IMPEXP     __DLL_IMPORT__
# elif defined (USE_LIBTIFF_STATIC)
#  define LIBTIFF_DLL_IMPEXP
# else /* assume USE_LIBTIFF_DLL */
#  define LIBTIFF_DLL_IMPEXP     __DLL_IMPORT__
# endif
#else /* WIN32 */
# define LIBTIFF_DLL_IMPEXP
#endif

#include "DPX.h"
#include "readframedpx.h"
#include "readframeexr.h"
#include "readframetiff.h"
#include "readframejpg.h"
#include "readframepng.h"
#include "wav.h"
#include "aeoexception.h"

#include "FilmScan.h"

#ifdef Q_OS_WIN32
#define isslash(c) (((c)=='/')||((c)=='\\'))
#else
#define isslash(c) ((c)=='/')
#endif

#ifdef USELIBAV
//-----------------------------------------------------------------------------
Video::~Video()
{
	if(frameNative) av_frame_free(&frameNative);
	if(frameGray16)
	{
		av_frame_free(&frameGray16);
	}
	if(frameRGB)
	{
		av_frame_free(&frameRGB);
	}
	if(convertGray16) sws_freeContext(convertGray16);
	if(convertRGB) sws_freeContext(convertRGB);
	if(codec) avcodec_close(codec);
	if(format) avformat_close_input(&format);
}

//----------------------------------------------------------------------------
bool Video::ReadNextFrame(size_t currfnum)
{
	AVPacket packet;
	int done;
	int frame_id;

	//if(this->frameNative == NULL) this->frameNative = av_frame_alloc();

	while(av_read_frame(this->format, &packet)>=0)
	{
		// Is this a packet from the right stream?
		if(packet.stream_index == this->streamIdx)
		{
			// Decode video frame
			avcodec_decode_video2(this->codec, this->frameNative, &done,
					&packet);

			// Did we get a complete video frame?
			if(done)
			{
				this->dts = packet.dts;
				this->curFrame = currfnum;
				// this->curFrame = (this->dts - this->dtsBase)/this->dtsStep + 1;

				av_packet_unref(&packet);
				return true;
			}
		}
		av_packet_unref(&packet);
	}
	this->dts = 0;
	this->curFrame = 0;

	return false;
}

bool Video::ReadFrame(size_t frameNum = 0)
{

	//if(frameNum == 0) return ReadNextFrame();

	// asking for the current frame again?
	if(this->curFrame == frameNum) return true;

	// asking for the next frame explicitly?
	if(this->curFrame+1 == frameNum) return ReadNextFrame(frameNum);

	size_t target = this->dtsBase + (frameNum)*this->dtsStep;

	int seekflags = AVSEEK_FLAG_ANY;

	if(target < this->dts) seekflags |= AVSEEK_FLAG_BACKWARD;

	av_seek_frame(this->format, this->streamIdx, target, seekflags);

	// do
	// {
	//    if(!ReadNextFrame()) return false;
	// } while(this->dts < target);
	if(!ReadNextFrame(frameNum)) return false;

	return true;
}

double *Video::GetFrame(size_t frameNum, double *buf=NULL)
{

	if(buf == NULL)
	{
		buf = new double [this->codec->width * this->codec->height];
		if(buf == NULL)
		{
			throw AeoException("Out of Memory: DPX buf");
		}
	}

	if(this->ReadFrame(frameNum) == false)
	{
		throw AeoException(
				QString("Could not read requested frame number: %1").
					arg(frameNum));
	}

	//if(this->frameGray16 == NULL) this->frameGray16 = av_frame_alloc();

	// Convert the image from its native format to Gray16
	sws_scale(this->convertGray16,
			(uint8_t const * const *)this->frameNative->data,
			this->frameNative->linesize, 0, this->codec->height,
			this->frameGray16->data, this->frameGray16->linesize);

	// translate Gray16 to double:
	const uint16_t *buf16;
	double scale = 1.0/(0x00FFFF);
	long i(0);
	for(int y=0; y<this->codec->height; ++y)
	{
		buf16 = (uint16_t *)
				(this->frameGray16->data[0] + y*this->frameGray16->linesize[0]);

		for(int x=0; x<this->codec->width; ++x)
			buf[i++] = double(buf16[x]) * scale;
	}

	return buf;
}


unsigned char *Video::GetFrameImage(size_t frameNum, unsigned char *buf,
		int &width,int &height,bool &endian)
{
	if(buf == NULL)
	{
		buf = new unsigned char [this->codec->width * this->codec->height * 8];
		if(buf == NULL)
		{
			throw AeoException("Out of Memory: video buf");
		}
	}

	if(this->ReadFrame(frameNum) == false)
	{
		throw AeoException(
				QString("Could not read requested frame number: %1").
					arg(frameNum));
	}

	// Convert the image from its native format to RGB
	sws_scale(this->convertRGB,
			(uint8_t const * const *)this->frameNative->data,
			this->frameNative->linesize, 0, this->codec->height,
			this->frameRGB->data, this->frameRGB->linesize);

	width = this->codec->width;
	height = this->codec->height;
	endian = false;
	memcpy(buf,this->frameRGB->data[0],width*height*8);

	return buf;
}
#endif

//-----------------------------------------------------------------------------
FilmFrame::FilmFrame(unsigned int r, unsigned int c)
{
	rows = r;
	cols = c;
	buf.resize(r*c);
}

//-----------------------------------------------------------------------------
FilmFrame::~FilmFrame()
{
}

//-----------------------------------------------------------------------------

const char *SourceFormatName[] {
	"DPX",
	"OpenEXR",
	"TIFF",
	"JPEG",
	"Other image",
	"LibAV Video",
	"WAV",
	"PNG",
	"Unknown" };

const char *FilmScan::GetFormatStr() const
{
	return SourceFormatName[this->srcFormat];
}

SourceFormat FilmScan::StrToSourceFormat(const char *str)
{
	SourceFormat i;
	for(i=0; i<SOURCE_UNKNOWN; ++i)
	{
		if(strcmp(SourceFormatName[i], str) == 0) break;
	}
	return i;
}

double FilmScan::SourceFpsHint() const
{
	if(this->numFrames <= 0)
		return 0.0;
#ifdef USELIBAV
	if(this->srcFormat == SOURCE_LIBAV &&
			this->vid &&
			this->vid->format &&
			this->vid->streamIdx >= 0 &&
			this->vid->streamIdx < int(this->vid->format->nb_streams))
	{
		AVStream *stream = this->vid->format->streams[this->vid->streamIdx];
		auto ratioToDouble = [](const AVRational &value) -> double
		{
			if(value.num <= 0 || value.den <= 0) return 0.0;
			return double(value.num) / double(value.den);
		};

		double fps = ratioToDouble(stream->avg_frame_rate);
		if(fps <= 0.0)
			fps = ratioToDouble(stream->r_frame_rate);
		if(fps <= 0.0 && stream->codec &&
				stream->codec->time_base.num > 0 &&
				stream->codec->time_base.den > 0)
		{
			fps = double(stream->codec->time_base.den) /
					double(stream->codec->time_base.num);
		}
		if(fps > 0.0 && fps < 240.0)
			return fps;
	}
#endif
	if(this->srcFormat == SOURCE_WAV)
		return 24.0;
	return 0.0;
}

//-----------------------------------------------------------------------------
bool FilmScan::Source(const std::string filename, SourceFormat fmt)
{
	this->Reset();
	this->inputName = filename;

	auto tryAutoDetect = [&](QString *detailsOut = NULL) -> bool
	{
		QStringList errors;

		try
		{
			if(SourceDPX(filename)) return true;
		}
		catch(std::exception &e)
		{
			errors << QString("DPX: %1").arg(e.what());
		}
		catch(...)
		{
			errors << "DPX: onbekende fout";
		}

		try
		{
			if(SourceTIFF(filename)) return true;
		}
		catch(std::exception &e)
		{
			errors << QString("TIFF: %1").arg(e.what());
		}
		catch(...)
		{
			errors << "TIFF: onbekende fout";
		}

		try
		{
			if(SourceJPG(filename)) return true;
		}
		catch(std::exception &e)
		{
			errors << QString("JPG: %1").arg(e.what());
		}
		catch(...)
		{
			errors << "JPG: onbekende fout";
		}

		try
		{
			if(SourcePNG(filename)) return true;
		}
		catch(std::exception &e)
		{
			errors << QString("PNG: %1").arg(e.what());
		}
		catch(...)
		{
			errors << "PNG: onbekende fout";
		}

		try
		{
			if(SourceWav(filename)) return true;
		}
		catch(std::exception &e)
		{
			errors << QString("WAV: %1").arg(e.what());
		}
		catch(...)
		{
			errors << "WAV: onbekende fout";
		}

		try
		{
			if(SourceLibAV(filename)) return true;
		}
		catch(std::exception &e)
		{
			errors << QString("Video/LibAV: %1").arg(e.what());
		}
		catch(...)
		{
			errors << "Video/LibAV: onbekende fout";
		}

		if(detailsOut)
			*detailsOut = errors.join("\n");

		return false;
	};

	if(fmt != SOURCE_UNKNOWN)
	{
		this->srcFormat = fmt;
		switch(fmt)
		{
		case SOURCE_DPX:
			if(SourceDPX(filename)) return true;
			break;
		case SOURCE_TIFF:
			if(SourceTIFF(filename)) return true;
			break;
		case SOURCE_JPEG:
			if(SourceJPG(filename)) return true;
			break;
		case SOURCE_PNG:
			if(SourcePNG(filename)) return true;
			break;
		case SOURCE_OTHER_IMG:
			try
			{
				if(SourceDPX(filename)) return true;
			}
			catch(...) {;}
			try
			{
				if(SourceTIFF(filename)) return true;
			}
			catch(...) {;}
			try
			{
				if(SourceJPG(filename)) return true;
			}
			catch(...) {;}
			try
			{
				if(SourcePNG(filename)) return true;
			}
			catch(...) {;}
			throw AeoException("Unable to recognize image source file");
			break;
		case SOURCE_LIBAV:
			if(SourceLibAV(filename)) return true;
			break;
		case SOURCE_WAV:
			if(SourceWav(filename)) return true;
			break;
		default:
			throw AeoException("Unrecognized FileType Request");
		}

		// If the user selected a restrictive file filter that does not match
		// the actual content, fall back to auto-detection before failing.
		QString autoDetectErrors;
		if(tryAutoDetect(&autoDetectErrors))
			return true;
		if(!autoDetectErrors.isEmpty())
		{
			QString msg("Unable to recognize source file\n\n");
			msg += autoDetectErrors;
			throw AeoException(msg);
		}
	}
	else
	{
		QString autoDetectErrors;
		if(tryAutoDetect(&autoDetectErrors))
			return true;
		if(!autoDetectErrors.isEmpty())
		{
			QString msg("Unable to recognize source file\n\n");
			msg += autoDetectErrors;
			throw AeoException(msg);
		}
	}

	throw AeoException("Unable to recognize source file");
}

//-----------------------------------------------------------------------------
void FilmScan::Reset()
{
	if(name) { delete [] name; name = NULL; }
	if(path) { delete [] path; path = NULL; }
	if(fnbuf) { delete [] fnbuf; fnbuf = NULL; }
	srcFormat = SOURCE_UNKNOWN;
	firstFrame = 0;
	numFrames = 0;
	width = 0;
	height = 0;
	#ifdef USELIBAV
	if(vid) { delete vid; vid = NULL; }
	#endif
	if(synth) {delete synth; synth = NULL; }
	if(!generatedFrameDir.isEmpty())
	{
		QDir cleanupDir(generatedFrameDir);
		if(cleanupDir.exists())
			cleanupDir.removeRecursively();
		generatedFrameDir.clear();
	}
	explicitFramePath.clear();
	resolvedFramePathByNumber.clear();

	inputName.clear();
}

//-----------------------------------------------------------------------------
bool FilmScan::SourceDPX(const std::string filename)
{
	// verify the file is a DPX and get the width and height from the header
	InStream img;
	dpx::Reader dpx;

	if(!img.Open(filename.c_str()))
	{
		QString msg;
		int err;

		msg += QString("FilmScan: Cannot open ");
		msg += QString(filename.c_str());
		msg += QString("\n");
		if((err=errno)!=0) msg += QString(strerror(err));
		throw AeoException(msg);
	}

	dpx.SetInStream(&img);
	if(!dpx.ReadHeader())
	{
		QString msg;
		int err;

		msg += QString("FilmScan: Invalid DPX header in ");
		msg += QString(filename.c_str());
		img.Close();
		throw AeoException(msg);
	}

	this->width = dpx.header.Width();
	this->height = dpx.header.Height();
	this->srcFormat = SOURCE_DPX;
	img.Close();

	SourceIdentifyImageSet(filename);

	// Pull the TimeCode from the first DPX in the sequence:
	sprintf(this->fnbuf+strlen(this->path)+1, this->name, this->FirstFrame());
	if(!img.Open(this->fnbuf))
	{
		QString msg;
		int err;

		msg += QString("FilmScan: Cannot open ");
		msg += QString(this->fnbuf);
		msg += QString("\n");
		if((err=errno)!=0) msg += QString(strerror(err));
		throw AeoException(msg);
	}

	dpx::Reader firstDPX;
	firstDPX.SetInStream(&img);
	if(!firstDPX.ReadHeader())
	{
		QString msg;
		int err;

		msg += QString("FilmScan: Invalid DPX header in ");
		msg += QString(this->fnbuf);
		img.Close();
		throw AeoException(msg);
	}

	char TC[16];
	firstDPX.header.TimeCode(TC);
	this->TimeCode = TC;
	img.Close();

	return true;
}

//-----------------------------------------------------------------------------
bool FilmScan::SourceEXR(const std::string filename)
{
#ifdef USE_OPENEXR
	// Open as an EXR and get the width and height
	// The Imf library will throw an exception if the file is not an EXR
	Imf::RgbaInputFile exr(filename.c_str());
	Imath::Box2i dw = exr.dataWindow();
	this->width = dw.max.x - dw.min.x + 1;
	this->height = dw.max.y - dw.min.y + 1;
	this->fmt = SOURCE_EXR;

	SourceIdentifyImageSet(filename);

	return true;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
bool FilmScan::SourceTIFF(const std::string filename)
{
	// verify the file is a TIFF and get the width and height from the header
	InStream img;

	TIFF *tif;

	if((tif = TIFFOpen(filename.c_str(), "r")) == NULL)
	{
		QString msg;
		int err;

		msg += QString("FilmScan: Cannot open ");
		msg += QString(filename.c_str());
		msg += QString("\n");
		if((err=errno)!=0) msg += QString(strerror(err));
		throw AeoException(msg);
	}

	// just grab the width and height from the header
	uint32 w, h;
	if(TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w) != 1 ||
			TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h) != 1)
	{
		TIFFClose(tif);
		throw AeoException("Invalid image size specification in TIFF.");
	}

	TIFFClose(tif);

	this->width = w;
	this->height = h;
	this->srcFormat = SOURCE_TIFF;

	this->TimeCode = "00:00:00:00";
	SourceIdentifyImageSet(filename);

	return true;
}

//-----------------------------------------------------------------------------
bool FilmScan::SourceJPG(const std::string filename)
{
	QString qfilename = QString::fromUtf8(filename.c_str());

	QImageReader reader(qfilename);
	reader.setAutoTransform(true);
	reader.setDecideFormatFromContent(true);
	QSize size = reader.size();
	QByteArray detectedFormat = reader.format().toLower();

	if(!size.isValid())
	{
		QImage img = reader.read();
		if(img.isNull())
		{
			QString msg("FilmScan: Cannot open ");
			msg += qfilename;
			if(!reader.errorString().isEmpty())
			{
				msg += "\n";
				msg += reader.errorString();
			}
			throw AeoException(msg);
		}
		size = img.size();
		if(detectedFormat.isEmpty())
			detectedFormat = reader.format().toLower();
	}

	if(!detectedFormat.isEmpty() &&
			detectedFormat != "jpeg" &&
			detectedFormat != "jpg")
	{
		QString msg("FilmScan: source is not a JPEG image: ");
		msg += qfilename;
		throw AeoException(msg);
	}

	this->width = size.width();
	this->height = size.height();
	this->srcFormat = SOURCE_JPEG;

	this->TimeCode = "00:00:00:00";
	SourceIdentifyImageSet(filename);

	return true;
}

//-----------------------------------------------------------------------------
bool FilmScan::SourcePNG(const std::string filename)
{
	QString qfilename = QString::fromUtf8(filename.c_str());

	QImageReader reader(qfilename);
	reader.setAutoTransform(true);
	reader.setDecideFormatFromContent(true);
	QSize size = reader.size();
	QByteArray detectedFormat = reader.format().toLower();

	if(!size.isValid())
	{
		QImage img = reader.read();
		if(img.isNull())
		{
			QString msg("FilmScan: Cannot open ");
			msg += qfilename;
			if(!reader.errorString().isEmpty())
			{
				msg += "\n";
				msg += reader.errorString();
			}
			throw AeoException(msg);
		}
		size = img.size();
		if(detectedFormat.isEmpty())
			detectedFormat = reader.format().toLower();
	}

	if(!detectedFormat.isEmpty() && detectedFormat != "png")
	{
		QString msg("FilmScan: source is not a PNG image: ");
		msg += qfilename;
		throw AeoException(msg);
	}

	this->width = size.width();
	this->height = size.height();
	this->srcFormat = SOURCE_PNG;

	this->TimeCode = "00:00:00:00";
	SourceIdentifyImageSet(filename);

	return true;
}

//-----------------------------------------------------------------------------
bool FilmScan::SourceIdentifyImageSet(const std::string filename)
{
	const QString qfilename = QString::fromUtf8(filename.c_str());
	const QFileInfo sourceInfo(qfilename);
	const QString fileName = sourceInfo.fileName().trimmed();
	const QString extension = sourceInfo.suffix();
	QString dirPath = sourceInfo.absolutePath();
	if(dirPath.isEmpty()) dirPath = ".";

	auto assignPathNameAndBuffer = [this](
			const QString &pathValue,
			const QString &nameValue,
			int reserveExtraChars) -> void
	{
		QString normalizedPath = pathValue.isEmpty() ? "." : pathValue;
		#ifdef Q_OS_WIN32
		normalizedPath.replace("\\", "/");
		#endif

		QByteArray pathUtf8 = normalizedPath.toUtf8();
		QByteArray nameUtf8 = nameValue.toUtf8();
		if(this->path) { delete [] this->path; this->path = NULL; }
		if(this->name) { delete [] this->name; this->name = NULL; }
		if(this->fnbuf) { delete [] this->fnbuf; this->fnbuf = NULL; }

		this->path = new char[pathUtf8.size() + 1];
		strcpy(this->path, pathUtf8.constData());
		this->name = new char[nameUtf8.size() + 1];
		strcpy(this->name, nameUtf8.constData());

		const int totalLen = pathUtf8.size() + 1 + nameUtf8.size() +
				std::max(0, reserveExtraChars) + 1;
		this->fnbuf = new char[totalLen];
		strcpy(this->fnbuf, pathUtf8.constData());
		this->fnbuf[pathUtf8.size()] = '/';
		this->fnbuf[pathUtf8.size() + 1] = '\0';
	};

	auto setSingleFrameSource = [&]() -> bool
	{
		QString fixedName = fileName.isEmpty() ?
				QString::fromUtf8(filename.c_str()) : fileName;
		// Escape percent so sprintf with fixed names remains safe.
		fixedName.replace("%", "%%");
		assignPathNameAndBuffer(dirPath, fixedName, 0);
		this->firstFrame = 1;
		this->numFrames = 1;
		this->explicitFramePath = sourceInfo.absoluteFilePath();
		this->resolvedFramePathByNumber.clear();
		this->resolvedFramePathByNumber.insert(1, sourceInfo.absoluteFilePath());
		return true;
	};

	if(fileName.isEmpty() || extension.isEmpty())
		return setSingleFrameSource();

	const QString stem = sourceInfo.completeBaseName();
	int digitStart = stem.size();
	while(digitStart > 0 && stem.at(digitStart - 1).isDigit())
		--digitStart;
	const QString prefix = stem.left(digitStart);
	const QString digits = stem.mid(digitStart);
	if(digits.isEmpty())
		return setSingleFrameSource();

	bool okInputFrame = false;
	const long inputFrame = digits.toLong(&okInputFrame);
	if(!okInputFrame)
		return setSingleFrameSource();

	const int numDigits = digits.size();
	std::vector<long> frames;
	frames.reserve(1024);
	QMap<long, QString> discoveredFramePaths;

	QDir dir(dirPath);
	const QFileInfoList entries = dir.entryInfoList(
			QDir::Files | QDir::NoSymLinks | QDir::Readable,
			QDir::Name);
	for(const QFileInfo &entry : entries)
	{
		if(entry.suffix().compare(extension, Qt::CaseInsensitive) != 0)
			continue;
		const QString entryStem = entry.completeBaseName();
		if(!entryStem.startsWith(prefix))
			continue;
		const QString entryDigits = entryStem.mid(prefix.size());
		if(entryDigits.size() != numDigits)
			continue;
		bool allDigits = true;
		for(int i = 0; i < entryDigits.size(); ++i)
		{
			if(!entryDigits.at(i).isDigit())
			{
				allDigits = false;
				break;
			}
		}
		if(!allDigits)
			continue;

		bool okFrame = false;
		const long frameNum = entryDigits.toLong(&okFrame);
		if(okFrame)
		{
			frames.push_back(frameNum);
			discoveredFramePaths.insert(frameNum, entry.absoluteFilePath());
		}
	}

	if(frames.empty())
		return setSingleFrameSource();

	std::sort(frames.begin(), frames.end());
	std::vector<long>::iterator framei = std::lower_bound(
			frames.begin(), frames.end(), inputFrame);
	if(framei == frames.end() || *framei != inputFrame)
		return setSingleFrameSource();

	long firstIdx(framei - frames.begin());
	while(firstIdx > 0 && frames[firstIdx-1] == frames[firstIdx] - 1)
		firstIdx--;
	this->firstFrame = frames[firstIdx];

	long lastIdx(framei - frames.begin());
	while(lastIdx + 1 < long(frames.size()) &&
			frames[lastIdx+1] == frames[lastIdx] + 1)
		lastIdx++;
	this->numFrames = frames[lastIdx] - this->firstFrame + 1;

	QString patternName = prefix;
	patternName.replace("%", "%%");
	patternName += QString("%%0%1ld.%2").arg(numDigits).arg(extension);
	assignPathNameAndBuffer(dirPath, patternName, 16);
	this->explicitFramePath = (this->numFrames == 1) ?
			sourceInfo.absoluteFilePath() : QString();
	this->resolvedFramePathByNumber.clear();
	for(long idx = firstIdx; idx <= lastIdx; ++idx)
	{
		const long frameValue = frames[idx];
		if(discoveredFramePaths.contains(frameValue))
			this->resolvedFramePathByNumber.insert(
					frameValue, discoveredFramePaths.value(frameValue));
	}
	return true;
}


//-----------------------------------------------------------------------------
bool FilmScan::SourceWav(const std::string filename)
{
	this->synth = new wav;
	if(!synth->read(filename.c_str()))
		return false;

	// set 10% overlap
	this->height = (unsigned int)((double(synth->samplesPerSec) / 24.0) * 1.1);
	this->width = this->height;

	this->firstFrame = 1;

	// round down to whole seconds (ignoring frames in the last partial second)
	this->numFrames = (synth->bufSize / synth->samplesPerSec) * 24;

	// and reduce by one more, for good measure, in case the last frame
	// completed the time second (to ensure the 10% overlap has something to
	// overlap into).
	this->numFrames--;

	int len = filename.length();
	const char *cp = filename.c_str() + len;
	while(cp>filename.c_str() && *cp != '/' && *cp != '\\') cp--;
	this->name = new char[strlen(cp)+1];
	strcpy(this->name, cp);
	this->path = new char[cp - filename.c_str() + 1];
	strncpy(this->path, filename.c_str(), cp-filename.c_str());
	this->TimeCode = "00:00:00:00";
	this->srcFormat = SOURCE_WAV;

	return true;
}

//-----------------------------------------------------------------------------

bool FilmScan::SourceLibAV(const std::string filename)
{
	#ifdef USELIBAV
	{
	// accept any recognized codec
	av_register_all();

	vid = new Video;

	AVDictionary *dict = NULL;

	// Open video file
	if(avformat_open_input(&vid->format, filename.c_str(), NULL, &dict)<0)
	{
		av_dict_free(&dict);
		delete vid;
		vid = NULL;
		return false; // Couldn't open file
	}

	av_dict_free(&dict);
	dict = NULL;

	// Retrieve stream information
	if(avformat_find_stream_info(vid->format, NULL) < 0)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		return false; // Couldn't find stream information
	}

	// Dump information about file onto standard error
	//av_dump_format(vid->format, 0, filename.c_str(), 0);
	char TC [20];
	AVDictionaryEntry * DE=NULL;
	// Find the first video stream
	vid->streamIdx = -1;
	for(int i=0; i<vid->format->nb_streams; ++i)
	{
		if(vid->format->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
		{
			vid->streamIdx=i;
			DE = av_dict_get(vid->format->streams[i]->metadata,"timecode",NULL ,0);

			if(DE)
				this->TimeCode = DE->value;
			else
				this->TimeCode = "00:00:00:00" ;

			break;
		}
	}

	if(vid->streamIdx == -1)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		return false; // Couldn't find any video stream
	}

    #ifdef ASK_TO_BUFFER_VIDEO
	int ret = QMessageBox::question(NULL,
		"Buffer Input?",
		"Do you want to buffer the incoming video?",
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::Yes);

	if(ret == QMessageBox::No)
	{
		av_dict_set(&dict, "fflags", "nobuffer",0);
		avformat_close_input(&(vid->format));
		delete vid;
		vid = new Video;

		if(avformat_open_input(&vid->format, filename.c_str(), NULL, &dict)<0)
		{
			av_dict_free(&dict);
			delete vid;
			vid = NULL;
			return false; // Couldn't open file
		}

		if(avformat_find_stream_info(vid->format, NULL) < 0)
		{
			avformat_close_input(&(vid->format));
			delete vid;
			vid = NULL;
			return false; // Couldn't find stream information
		}
		// Find the first video stream
		vid->streamIdx = -1;
		for(int i=0; i<vid->format->nb_streams; ++i)
		{
			if(vid->format->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
			{
				vid->streamIdx=i;
				break;
			}
		}

		if(vid->streamIdx == -1)
		{
			avformat_close_input(&(vid->format));
			delete vid;
			vid = NULL;
			return false; // Couldn't find any video stream
		}
    }
    #endif

	// ask the stream how many frames it has
	if((this->numFrames=vid->format->streams[vid->streamIdx]->nb_frames) == 0)
	{
		// TODO: if it doesn't know, guess based on duration and then validate.
		//
		this->numFrames =
				(int)(( vid->format->streams[vid->streamIdx]->duration /
					(double)AV_TIME_BASE ) *
					vid->format->streams[vid->streamIdx]->codec->time_base.den +
					0.5);

		/* FAILED ATTEMPT TO USE LIBAV TO READ IMAGES
		if(this->numFrames < 0)
		{
			SourceIdentifyImageSet(filename);
		}
		*/

		if(this->numFrames <= 0)
		{
			avformat_close_input(&(vid->format));
			delete vid;
			vid = NULL;
			throw AeoException("Could not determine number of frames in video");
		}
	}

	this->firstFrame = 0;

	AVCodecContext *codecOrig;
	AVCodec *decoder;

	// Get a pointer to the codec context for the video stream
	codecOrig=vid->format->streams[vid->streamIdx]->codec;

	// Find the decoder for the video stream
	decoder=avcodec_find_decoder(codecOrig->codec_id);
	if(decoder==NULL)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		throw AeoException("Unsupported codec.");
	}

	// Copy codec context
	vid->codec = avcodec_alloc_context3(decoder);
	if(avcodec_copy_context(vid->codec, codecOrig) != 0)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		throw AeoException("Couldn't copy codec context");
	}
	avcodec_close(codecOrig);

	// Open codec
	if(avcodec_open2(vid->codec, decoder, NULL)<0)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		throw AeoException("Couldn't open decoder context");
	}

	// Allocate video frame
	vid->frameNative=av_frame_alloc();
	if(vid->frameNative==NULL)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		throw AeoException("Couldn't allocate frame structure");
	}

	// Allocate an RGB frame
	vid->frameRGB=av_frame_alloc();
	if(vid->frameRGB==NULL)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		throw AeoException ("Couldn't allocate frame structure");
	}

	this->width = vid->codec->width;
	this->height = vid->codec->height;

	// allocate RGB buffer (to be freed when vid is destroyed)
	uint8_t *buffer = (uint8_t *)av_malloc(
			av_image_get_buffer_size(
					AV_PIX_FMT_RGBA64LE,
					vid->codec->width, vid->codec->height,1));

	// Assign appropriate parts of buffer to image planes in frameRGB
	// Note that frameRGB is an AVFrame, but AVFrame is a superset
	// of AVPicture
	//avpicture_fill((AVPicture *)vid->frameRGB, buffer, AV_PIX_FMT_RGBA64LE,
	//		vid->codec->width, vid->codec->height);

	av_image_fill_arrays(vid->frameRGB->data, vid->frameRGB->linesize, buffer,
			AV_PIX_FMT_RGBA64LE, vid->codec->width, vid->codec->height, 1);

	// Allocate a Gray16 frame
	vid->frameGray16=av_frame_alloc();
	if(vid->frameGray16==NULL)
	{
		avformat_close_input(&(vid->format));
		delete vid;
		vid = NULL;
		throw AeoException("Couldn't allocate frame structure");
	}
	// allocate Gray16 buffer (to be freed when vid is destroyed)
	buffer = (uint8_t *)av_malloc(
			av_image_get_buffer_size(
					AV_PIX_FMT_GRAY16, vid->codec->width, vid->codec->height, 1
					));

	// Assign appropriate parts of buffer to image planes in frameGray16
	//avpicture_fill((AVPicture *)vid->frameGray16, buffer, AV_PIX_FMT_GRAY16,
	//		vid->codec->width, vid->codec->height);
	av_image_fill_arrays(vid->frameGray16->data, vid->frameGray16->linesize,
			buffer, AV_PIX_FMT_GRAY16,
			vid->codec->width, vid->codec->height, 1);

	// initialize SWS context for conversion to RGB
	vid->convertRGB = sws_getContext(
			vid->codec->width,
			vid->codec->height,
			vid->codec->pix_fmt,
			vid->codec->width,
			vid->codec->height,
			AV_PIX_FMT_RGBA64LE, //rgba
			SWS_BILINEAR,
			NULL,
			NULL,
			NULL
			);

	// initialize SWS context for conversion to Gray16
	vid->convertGray16 = sws_getContext(
			vid->codec->width,
			vid->codec->height,
			vid->codec->pix_fmt,
			vid->codec->width,
			vid->codec->height,
			AV_PIX_FMT_GRAY16,
			SWS_BILINEAR,
			NULL,
			NULL,
			NULL
			);


	// Read two frames to calibrate seek-to-frame parameters
	vid->ReadNextFrame(0);
	vid->dtsBase = vid->dts;
	vid->ReadNextFrame(1);
	vid->dtsStep = vid->dts;

	// reset to first frame
	vid->ReadFrame(0);

	int len = filename.length();
	const char *cp = filename.c_str() + len;
	while(cp>filename.c_str() && *cp != '/' && *cp != '\\') cp--;
	this->name = new char[strlen(cp)+1];
	strcpy(this->name, cp);
	this->path = new char[cp - filename.c_str() + 1];
	strncpy(this->path, filename.c_str(), cp-filename.c_str());

	this->srcFormat = SOURCE_LIBAV;

	return true;
	}
	#else
	{
		QString qfilename = QString::fromUtf8(filename.c_str());
		QFileInfo srcInfo(qfilename);
		if(!srcInfo.exists() || !srcInfo.isFile())
			return false;

		QString ffmpegProgram;
		#ifdef Q_OS_WIN32
		{
			const QString bundled = QDir(
					QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe");
			if(QFileInfo::exists(bundled))
				ffmpegProgram = bundled;
			else
				ffmpegProgram = "ffmpeg";
		}
		#else
		ffmpegProgram = "ffmpeg";
		#endif

		const QString outDirPath = QDir(QDir::tempPath()).filePath(
				QString("aeo-light-video-%1")
				.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
		if(!QDir().mkpath(outDirPath))
			return false;

		const QString framePattern = QDir(outDirPath).filePath("frame_%06d.png");
		QProcess ffmpeg;
		QStringList args;
		args << "-hide_banner"
			 << "-loglevel" << "error"
			 << "-nostdin"
			 << "-y"
			 << "-i" << qfilename
			 << framePattern;
		ffmpeg.start(ffmpegProgram, args);
		if(!ffmpeg.waitForStarted(5000))
		{
			QString msg("Unable to start ffmpeg for video decoding.");
			const QString err = ffmpeg.errorString().trimmed();
			if(!err.isEmpty())
				msg += "\n" + err;
			QDir(outDirPath).removeRecursively();
			throw AeoException(msg);
		}
		if(!ffmpeg.waitForFinished(-1) ||
				ffmpeg.exitStatus() != QProcess::NormalExit ||
				ffmpeg.exitCode() != 0)
		{
			const QString ffmpegErr = QString::fromUtf8(ffmpeg.readAllStandardError());
			QDir(outDirPath).removeRecursively();
			if(!ffmpegErr.trimmed().isEmpty())
			{
				QString msg("Unable to decode video source via ffmpeg:\n");
				msg += ffmpegErr.trimmed();
				throw AeoException(msg);
			}
			return false;
		}

		const QDir outDir(outDirPath);
		const QStringList frames = outDir.entryList(
				QStringList() << "frame_*.png",
				QDir::Files,
				QDir::Name);
		if(frames.isEmpty())
		{
			QDir(outDirPath).removeRecursively();
			return false;
		}

		const QString firstFramePath = outDir.filePath(frames.first());
		try
		{
			if(SourcePNG(firstFramePath.toUtf8().constData()))
			{
				generatedFrameDir = outDirPath;
				return true;
			}
		}
		catch(...)
		{
			QDir(outDirPath).removeRecursively();
			throw;
		}

		QDir(outDirPath).removeRecursively();
		return false;
	}
	#endif
}

//-----------------------------------------------------------------------------
FilmScan::~FilmScan()
{
	if(this->name) delete [] this->name;
	if(this->path) delete [] this->path;
	if(this->fnbuf) delete [] this->fnbuf;
	if(!this->generatedFrameDir.isEmpty())
	{
		QDir cleanupDir(this->generatedFrameDir);
		if(cleanupDir.exists())
			cleanupDir.removeRecursively();
		this->generatedFrameDir.clear();
	}
	this->explicitFramePath.clear();
	this->resolvedFramePathByNumber.clear();
	#ifdef USELIBAV
	if(this->vid) delete this->vid;
	#endif
}


//-----------------------------------------------------------------------------

double* FilmScan::GetFrame(long frameNum, double *buf) const
{
	if(frameNum < this->FirstFrame() || frameNum > this->LastFrame())
	{
		throw AeoException(QString("Frame out of range: %1").arg(frameNum));
	}

	switch(this->srcFormat)
	{
	case SOURCE_DPX:
		sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
		buf = ReadFrameDPX(fnbuf, buf);
		break;
	case SOURCE_TIFF:
		sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
		buf = ReadFrameTIFF(fnbuf, buf);
		break;
	case SOURCE_JPEG:
		if(this->resolvedFramePathByNumber.contains(frameNum))
		{
			const QByteArray resolvedUtf8 =
					this->resolvedFramePathByNumber.value(frameNum).toUtf8();
			buf = ReadFrameJPG(resolvedUtf8.constData(), buf);
		}
		else if(!this->explicitFramePath.isEmpty() && this->numFrames == 1)
		{
			const QByteArray explicitUtf8 = this->explicitFramePath.toUtf8();
			buf = ReadFrameJPG(explicitUtf8.constData(), buf);
		}
		else
		{
			sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
			buf = ReadFrameJPG(fnbuf, buf);
		}
		break;
	case SOURCE_PNG:
		if(this->resolvedFramePathByNumber.contains(frameNum))
		{
			const QByteArray resolvedUtf8 =
					this->resolvedFramePathByNumber.value(frameNum).toUtf8();
			buf = ReadFramePNG(resolvedUtf8.constData(), buf);
		}
		else if(!this->explicitFramePath.isEmpty() && this->numFrames == 1)
		{
			const QByteArray explicitUtf8 = this->explicitFramePath.toUtf8();
			buf = ReadFramePNG(explicitUtf8.constData(), buf);
		}
		else
		{
			sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
			buf = ReadFramePNG(fnbuf, buf);
		}
		break;
	case SOURCE_LIBAV:
#ifdef USELIBAV
		if(this->vid) buf = this->vid->GetFrame(frameNum, buf);
		else throw AeoException("Internal video structure not ready");
#else
		throw AeoException("LibAV support is disabled in this build");
#endif
		break;
	case SOURCE_WAV:
		if(this->synth) buf = this->synth->GetFrame(frameNum, buf);
		else throw AeoException("Internal wav synth structure not ready");
	default:
		throw AeoException("Internal scan format not set correctly");
	}

#if 0
	#ifdef USELIBAV
	if(this->vid)
	{
		buf = this->vid->GetFrame(frameNum, buf);
	}
	else
	#endif
	if(this->synth)
	{
		buf = this->synth->GetFrame(frameNum, buf);
	}
	else
	{
		sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
		buf = ReadFrameDPX(fnbuf, buf);
	}

#endif

	return buf;
}




//-----------------------------------------------------------------------------

FrameTexture* FilmScan::GetFrameImage(long frameNum, FrameTexture *frame) const
{
	if(frameNum < this->FirstFrame() || frameNum > this->LastFrame())
	{
		throw AeoException(QString("Frame out of range: %1").arg(frameNum));
	}

	if(!frame) frame = new FrameTexture;

	switch(this->srcFormat)
	{
	case SOURCE_DPX:
		sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
		frame->buf = ReadFrameDPX_ImageData(fnbuf, frame->buf, frame->bufSize,
				frame->width, frame->height, frame->isNonNativeEndianess,
				frame->format, frame->nComponents);
		break;
	case SOURCE_TIFF:
		sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
		frame->buf = ReadFrameTIFF_ImageData(fnbuf, frame->buf,
				frame->width, frame->height, frame->isNonNativeEndianess,
				frame->format, frame->nComponents);
		break;
	case SOURCE_JPEG:
		if(this->resolvedFramePathByNumber.contains(frameNum))
		{
			const QByteArray resolvedUtf8 =
					this->resolvedFramePathByNumber.value(frameNum).toUtf8();
			frame->buf = ReadFrameJPG_ImageData(resolvedUtf8.constData(), frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess,
					frame->format, frame->nComponents);
		}
		else if(!this->explicitFramePath.isEmpty() && this->numFrames == 1)
		{
			const QByteArray explicitUtf8 = this->explicitFramePath.toUtf8();
			frame->buf = ReadFrameJPG_ImageData(explicitUtf8.constData(), frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess,
					frame->format, frame->nComponents);
		}
		else
		{
			sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
			frame->buf = ReadFrameJPG_ImageData(fnbuf, frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess,
					frame->format, frame->nComponents);
		}
		break;
	case SOURCE_PNG:
		if(this->resolvedFramePathByNumber.contains(frameNum))
		{
			const QByteArray resolvedUtf8 =
					this->resolvedFramePathByNumber.value(frameNum).toUtf8();
			frame->buf = ReadFramePNG_ImageData(resolvedUtf8.constData(), frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess,
					frame->format, frame->nComponents);
		}
		else if(!this->explicitFramePath.isEmpty() && this->numFrames == 1)
		{
			const QByteArray explicitUtf8 = this->explicitFramePath.toUtf8();
			frame->buf = ReadFramePNG_ImageData(explicitUtf8.constData(), frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess,
					frame->format, frame->nComponents);
		}
		else
		{
			sprintf(this->fnbuf+strlen(this->path)+1, this->name, frameNum);
			frame->buf = ReadFramePNG_ImageData(fnbuf, frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess,
					frame->format, frame->nComponents);
		}
		break;
	case SOURCE_LIBAV:
#ifdef USELIBAV
		if(this->vid)
		{
			frame->buf = this->vid->GetFrameImage(frameNum, frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess);
			frame->nComponents = 4;
			frame->format = GL_UNSIGNED_SHORT;
		}
		else throw AeoException("Internal video structure not ready");
#else
		throw AeoException("LibAV support is disabled in this build");
#endif
		break;
	case SOURCE_WAV:
		if(this->synth)
		{
			frame->buf = this->synth->GetFrameImage(frameNum, frame->buf,
					frame->width, frame->height, frame->isNonNativeEndianess);
			frame->nComponents = 4;
			frame->format = GL_UNSIGNED_SHORT;
		}
		else throw AeoException("Internal wav synth structure not ready");
	default:
		throw AeoException("Internal scan format not set correctly");
	}

	return frame;
}

//-----------------------------------------------------------------------------

FilmStrip FilmScan::GetFrameRange(long frameRange[2]) const
{
	if(frameRange[0] < this->FirstFrame() ||
			frameRange[1] < frameRange[0] ||
			frameRange[1] > this->LastFrame())
	{
		throw AeoException(
				QString("Frames out of range: %1 - %2").
					arg(frameRange[0]).arg(frameRange[1]));
	}

	long nFrames, i;
	unsigned int r, c;
	FilmStrip frames;

	nFrames = frameRange[1] - frameRange[0] + 1;
	r = this->Height();
	c = this->Width();

	frames.resize(nFrames, FilmFrame(r,c));

	for(i = 0; i<nFrames; i++)
	{
		(void)GetFrame(frameRange[0] + i, frames[i]);
	}

	return frames;
}


//-----------------------------------------------------------------------------

double* FilmScan::GetSoundSignal(long frameNum,
		unsigned int col0, unsigned int col1) const
{
	double *frame = this->GetFrame(frameNum, NULL);
	double *sound = new double[this->height];
	double *row;

	int r,c;

	// get the average value over the column range for each row
	for(r=0; r<this->height; r++)
	{
		sound[r] = 0;
		row = frame + r*this->width + col0;
		for(c=col0; c<=col1; c++)
			sound[r] += *(row++);
		sound[r] /= (col1-col0+1);
	}

	return sound;
}


//-----------------------------------------------------------------------------

SoundSignal FilmScan::GetSoundLocal(FilmFrame frame,
		unsigned int col0, unsigned int col1) const
{
	SoundSignal sound(this->height);

	unsigned int r,c;

	// get the average value over the column range for each row
	for(r=0; r<this->height; r++)
	{
		sound[r] = 0;
		for(c=col0; c<=col1; c++)
			sound[r] += double(frame[r][c]);
		sound[r] /= (col1-col0+1);
	}

	return sound;
}

