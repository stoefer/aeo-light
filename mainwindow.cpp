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

#include <algorithm>
#include <array>

#include <QApplication>
#include <QClipboard>
#include <QByteArray>
#include <QProcess>
#include <QDebug>
#include <QDialogButtonBox>
#include <QSettings>
#include <QVersionNumber>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDomDocument>
#include <QTimer>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QDesktopServices>
#include <QInputDialog>
#include <QFormLayout>
#include <QSoundEffect>
#include <QImage>
#include <QFile>
#include <QLabel>
#include <QMouseEvent>
#include <QDateTime>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTransform>
#include <QScrollArea>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStorageInfo>
#include <QScreen>
#include <QSpinBox>
#include <QCheckBox>
#include <QShortcut>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMap>
#include <QVBoxLayout>
#include <QSlider>
#include <QVariantMap>

#include <cstdio>
#include <exception>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <cstdint>
#include <limits>

#include <fcntl.h>
#include <stdlib.h>

#include <csetjmp>
#include <csignal>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "wav.h"
#include "writexml.h"
#include "project.h"
#include "aeoexception.h"

#include "mainwindow.h"
#include "savesampledialog.h"
#include "preferencesdialog.h"
#include "extractdialog.h"

#ifdef USE_MUX_HACK
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern "C"
{
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// The av_err2str(n) macro uses the C99 compound literal construct, which
// MSVC does not understand.
// Ref: https://ffmpeg.zeranoe.com/forum/viewtopic.php?t=4220
// Ref: https://stackoverflow.com/questions/3869963/compound-literals-in-msvc
#ifdef Q_CC_MSVC
#define aeo_av_err2str(n) "MSVC Cannot Show This Error"
#else
//#define aeo_av_err2str(n) av_err2str(n)
#define aeo_av_err2str(errnum) \
	av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), \
	AV_ERROR_MAX_STRING_SIZE, errnum)
#endif


#endif // ifdef USE_MUX_HACK

#ifndef USELIBAV
#define AV_LOG_INFO 0
#define av_log(...) do {} while(0)
#endif

static QString FormatEtaSeconds(double seconds)
{
	if(seconds < 0) seconds = 0;
	int total = static_cast<int>(seconds + 0.5);
	int hrs = total / 3600;
	int mins = (total % 3600) / 60;
	int secs = total % 60;

	if(hrs > 0)
	{
		return QString("%1:%2:%3")
				.arg(hrs, 2, 10, QChar('0'))
				.arg(mins, 2, 10, QChar('0'))
				.arg(secs, 2, 10, QChar('0'));
	}
	return QString("%1:%2")
			.arg(mins, 2, 10, QChar('0'))
			.arg(secs, 2, 10, QChar('0'));
}

static QString FormatBytesHumanReadable(qint64 bytes)
{
	double value = std::max<qint64>(0, bytes);
	const char *units[] = {"B", "KB", "MB", "GB", "TB"};
	int unitIndex = 0;
	while(value >= 1024.0 && unitIndex < 4)
	{
		value /= 1024.0;
		++unitIndex;
	}
	if(unitIndex == 0)
		return QString("%1 %2").arg(static_cast<qint64>(value)).arg(units[unitIndex]);
	return QString("%1 %2").arg(value, 0, 'f', 1).arg(units[unitIndex]);
}

static double ReadFpsFromUiIndex(int fpsIndex)
{
	switch(fpsIndex)
	{
	case 0: return 23.976;
	case 1: return 24.0;
	case 2: return 25.0;
	default: return 24.0;
	}
}

static QString BuildNumberedPath(const QString &requestedPath, int suffix)
{
	QFileInfo info(requestedPath);
	QString path = info.path();
	QString base = info.completeBaseName();
	QString ext = info.completeSuffix();
	QString numbered = QString("%1-%2").arg(base).arg(suffix);

	if(ext.isEmpty())
		return QDir(path).filePath(numbered);
	return QDir(path).filePath(numbered + "." + ext);
}

static QString VideoContainerExtensionForMode(int mode)
{
	switch(mode)
	{
	case VIDEO_CONTAINER_MOV: return ".mov";
	case VIDEO_CONTAINER_MKV: return ".mkv";
	case VIDEO_CONTAINER_MP4:
	default: return ".mp4";
	}
}

enum VideoResolutionMode
{
	VIDEO_RESOLUTION_SOURCE = 0,
	VIDEO_RESOLUTION_640X480 = 1,
	VIDEO_RESOLUTION_1280X720 = 2,
	VIDEO_RESOLUTION_1920X1080 = 3,
	VIDEO_RESOLUTION_2560X1440 = 4,
	VIDEO_RESOLUTION_3840X2160 = 5
};

static QSize VideoResolutionSizeForMode(int mode)
{
	switch(mode)
	{
	case VIDEO_RESOLUTION_640X480: return QSize(640, 480);
	case VIDEO_RESOLUTION_1280X720: return QSize(1280, 720);
	case VIDEO_RESOLUTION_1920X1080: return QSize(1920, 1080);
	case VIDEO_RESOLUTION_2560X1440: return QSize(2560, 1440);
	case VIDEO_RESOLUTION_3840X2160: return QSize(3840, 2160);
	case VIDEO_RESOLUTION_SOURCE:
	default:
		return QSize();
	}
}

static QString VideoResolutionLabelForMode(int mode)
{
	switch(mode)
	{
	case VIDEO_RESOLUTION_640X480: return "640x480 (SD)";
	case VIDEO_RESOLUTION_1280X720: return "1280x720 (HD)";
	case VIDEO_RESOLUTION_1920X1080: return "1920x1080 (Full HD)";
	case VIDEO_RESOLUTION_2560X1440: return "2560x1440 (QHD)";
	case VIDEO_RESOLUTION_3840X2160: return "3840x2160 (4K UHD)";
	case VIDEO_RESOLUTION_SOURCE:
	default:
		return QObject::tr("Bronresolutie (crop)");
	}
}

enum AudioQualityPresetMode
{
	AUDIO_QUALITY_PRESET_COMPACT = 0,
	AUDIO_QUALITY_PRESET_STANDARD = 1,
	AUDIO_QUALITY_PRESET_HIGH = 2,
	AUDIO_QUALITY_PRESET_CUSTOM = 3
};

enum QueueBatchReportMode
{
	QUEUE_BATCH_REPORT_OFF = 0,
	QUEUE_BATCH_REPORT_TXT = 1,
	QUEUE_BATCH_REPORT_JSON = 2,
	QUEUE_BATCH_REPORT_BOTH = 3
};

enum ExportReportMode
{
	EXPORT_REPORT_OFF = 0,
	EXPORT_REPORT_TXT = 1,
	EXPORT_REPORT_JSON = 2,
	EXPORT_REPORT_BOTH = 3
};

enum VideoTargetProfileMode
{
	VIDEO_TARGET_PROFILE_CUSTOM = 0,
	VIDEO_TARGET_PROFILE_QUICK_REVIEW = 1,
	VIDEO_TARGET_PROFILE_WEB_SHARE = 2,
	VIDEO_TARGET_PROFILE_ARCHIVE = 3
};

static QString ReadLastExportDirMain(const QString &key, const QString &fallback)
{
	QSettings settings;
	settings.beginGroup("default-folder");
	QString dir = settings.value(key, fallback).toString();
	settings.endGroup();
	if(dir.isEmpty()) dir = fallback;
	return dir;
}

static void WriteLastExportDirMain(const QString &key, const QString &dir)
{
	if(dir.isEmpty()) return;
	QSettings settings;
	settings.beginGroup("default-folder");
	settings.setValue(key, dir);
	if(key == "last-audio-export")
		settings.setValue("export", dir);
	settings.endGroup();
}

static bool ReadShowHelpTextSetting(bool *usedLegacyKey = nullptr)
{
	bool legacyUsed = false;
	QSettings settings;
	settings.beginGroup("ui");
	const bool hasUiSetting = settings.contains("show-help-text");
	bool showHelpText = settings.value("show-help-text", true).toBool();
	settings.endGroup();

	if(!hasUiSetting)
	{
		settings.beginGroup("extraction");
		if(settings.contains("show-audio-help-text"))
		{
			showHelpText = settings.value("show-audio-help-text", true).toBool();
			legacyUsed = true;
		}
		settings.endGroup();
	}

	if(usedLegacyKey)
		*usedLegacyKey = legacyUsed;
	return showHelpText;
}

static bool ReadCompactModeSetting()
{
	QSettings settings;
	settings.beginGroup("ui");
	const bool compactMode = settings.value("compact-mode", false).toBool();
	settings.endGroup();
	return compactMode;
}

static QVariantMap SerializeExtractedSoundParams(const ExtractedSound &params)
{
	QVariantMap map;
	map.insert("frameIn", static_cast<uint>(params.frameIn));
	map.insert("frameOut", static_cast<uint>(params.frameOut));
	map.insert("bounds0", static_cast<uint>(params.bounds[0]));
	map.insert("bounds1", static_cast<uint>(params.bounds[1]));
	map.insert("pixBounds0", static_cast<uint>(params.pixBounds[0]));
	map.insert("pixBounds1", static_cast<uint>(params.pixBounds[1]));
	map.insert("framePitch0", static_cast<uint>(params.framePitch[0]));
	map.insert("framePitch1", static_cast<uint>(params.framePitch[1]));
	map.insert("videoFrameCrop0", static_cast<uint>(params.videoFrameCrop[0]));
	map.insert("videoFrameCrop1", static_cast<uint>(params.videoFrameCrop[1]));
	map.insert("videoStableCrop", params.videoStableCrop);
	map.insert("videoDriftSensitivity", static_cast<uint>(params.videoDriftSensitivity));
	map.insert("gamma", static_cast<uint>(params.gamma));
	map.insert("gain", static_cast<uint>(params.gain));
	map.insert("sCurve", static_cast<uint>(params.sCurve));
	map.insert("overlap", static_cast<uint>(params.overlap));
	map.insert("lift", static_cast<int>(params.lift));
	map.insert("blur", static_cast<int>(params.blur));
	map.insert("fpsType", static_cast<int>(params.fpsType));
	map.insert("rot_angle", params.rot_angle);
	map.insert("useBounds", params.useBounds);
	map.insert("usePixBounds", params.usePixBounds);
	map.insert("useSCurve", params.useSCurve);
	map.insert("makeNegative", params.makeNegative);
	map.insert("makeGray", params.makeGray);
	map.insert("applyRotation", params.applyRotation);
	return map;
}

static ExtractedSound DeserializeExtractedSoundParams(const QVariantMap &map)
{
	ExtractedSound params;
	params.frameIn = map.value("frameIn", 0).toUInt();
	params.frameOut = map.value("frameOut", 0).toUInt();
	params.bounds[0] = static_cast<uint16_t>(map.value("bounds0", 0).toUInt());
	params.bounds[1] = static_cast<uint16_t>(map.value("bounds1", 0).toUInt());
	params.pixBounds[0] = static_cast<uint16_t>(map.value("pixBounds0", 0).toUInt());
	params.pixBounds[1] = static_cast<uint16_t>(map.value("pixBounds1", 0).toUInt());
	params.framePitch[0] = static_cast<uint16_t>(map.value("framePitch0", 0).toUInt());
	params.framePitch[1] = static_cast<uint16_t>(map.value("framePitch1", 0).toUInt());
	params.videoFrameCrop[0] = static_cast<uint16_t>(map.value("videoFrameCrop0", 100).toUInt());
	params.videoFrameCrop[1] = static_cast<uint16_t>(map.value("videoFrameCrop1", 100).toUInt());
	params.videoStableCrop = map.value("videoStableCrop", true).toBool();
	params.videoDriftSensitivity = static_cast<uint16_t>(
			map.value("videoDriftSensitivity", 100).toUInt());
	params.gamma = static_cast<uint16_t>(map.value("gamma", 100).toUInt());
	params.gain = static_cast<uint16_t>(map.value("gain", 100).toUInt());
	params.sCurve = static_cast<uint16_t>(map.value("sCurve", 0).toUInt());
	params.overlap = static_cast<uint8_t>(map.value("overlap", 20).toUInt());
	params.lift = static_cast<int8_t>(map.value("lift", 0).toInt());
	params.blur = static_cast<int8_t>(map.value("blur", 0).toInt());
	params.fpsType = static_cast<uint8_t>(map.value("fpsType", FPS_FILM).toUInt());
	params.rot_angle = map.value("rot_angle", 0.0).toDouble();
	params.useBounds = map.value("useBounds", true).toBool();
	params.usePixBounds = map.value("usePixBounds", false).toBool();
	params.useSCurve = map.value("useSCurve", false).toBool();
	params.makeNegative = map.value("makeNegative", false).toBool();
	params.makeGray = map.value("makeGray", false).toBool();
	params.applyRotation = map.value("applyRotation", false).toBool();
	return params;
}

static QVariantMap SerializeExtractTaskMap(const ExtractTask &task)
{
	QVariantMap map;
	map.insert("source", task.source);
	map.insert("srcFormat", static_cast<int>(task.srcFormat));
	map.insert("output", task.output);
	map.insert("params", SerializeExtractedSoundParams(task.params));
	return map;
}

static bool DeserializeExtractTaskMap(const QVariantMap &map, ExtractTask &task)
{
	task.source = map.value("source").toString().trimmed();
	task.srcFormat = static_cast<SourceFormat>(
			map.value("srcFormat", static_cast<int>(SOURCE_UNKNOWN)).toInt());
	task.output = map.value("output").toString().trimmed();
	task.params = DeserializeExtractedSoundParams(map.value("params").toMap());
	return !task.source.isEmpty() && !task.output.isEmpty();
}

static QString EnsureNonOverwritingPath(const QString &requestedPath)
{
	if(requestedPath.isEmpty()) return requestedPath;
	if(!QFileInfo::exists(requestedPath)) return requestedPath;

	int suffix = 1;
	QString candidate = BuildNumberedPath(requestedPath, suffix);
	while(QFileInfo::exists(candidate))
	{
		suffix++;
		candidate = BuildNumberedPath(requestedPath, suffix);
	}
	return candidate;
}

static QString SanitizeFilenamePart(QString value)
{
	value = value.trimmed();
	value.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
	value.replace(QRegularExpression("\\s+"), "_");
	while(value.contains("__"))
		value.replace("__", "_");
	value = value.trimmed();
	if(value.isEmpty())
		value = "output";
	return value;
}

struct AutoFrameStats
{
	double meanLuma = 0.0;
	double lumaLow = 0.0;
	double lumaHigh = 1.0;
	double channelSpread = 0.0;
	bool valid = false;
};

static double AutoProfileStrength(int profileMode)
{
	switch(profileMode)
	{
	case 0: return 0.60; // Conservatief
	case 2: return 1.15; // Sterk
	default: return 0.85; // Standaard
	}
}

static bool ComputeAutoFrameStats(const FrameTexture *frame, AutoFrameStats &stats)
{
	if(!frame || !frame->buf || frame->width <= 0 || frame->height <= 0)
		return false;

	const int64_t totalPixels = int64_t(frame->width) * int64_t(frame->height);
	if(totalPixels <= 0)
		return false;

	const int targetSamples = 200000;
	const int step = std::max(1, int(std::sqrt(
			double(totalPixels) / std::max(1, targetSamples))));

	double sumLuma = 0.0;
	double sumR = 0.0;
	double sumG = 0.0;
	double sumB = 0.0;
	int64_t samples = 0;
	std::array<int, 256> hist{};

	auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
	auto addSample = [&](double r, double g, double b)
	{
		r = clamp01(r);
		g = clamp01(g);
		b = clamp01(b);
		const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
		const int bin = std::max(0, std::min(255, int(std::round(luma * 255.0))));
		hist[size_t(bin)]++;
		sumLuma += luma;
		sumR += r;
		sumG += g;
		sumB += b;
		samples++;
	};

	if(frame->format == GL_UNSIGNED_BYTE)
	{
		const int comps = std::max(1, frame->nComponents);
		const uint8_t *src = reinterpret_cast<const uint8_t*>(frame->buf);
		for(int y = 0; y < frame->height; y += step)
		{
			for(int x = 0; x < frame->width; x += step)
			{
				const int idx = (y * frame->width + x) * comps;
				double r = 0.0, g = 0.0, b = 0.0;
				if(comps >= 3)
				{
					r = src[idx + 0] / 255.0;
					g = src[idx + 1] / 255.0;
					b = src[idx + 2] / 255.0;
				}
				else
				{
					r = g = b = src[idx] / 255.0;
				}
				addSample(r, g, b);
			}
		}
	}
	else if(frame->format == GL_UNSIGNED_SHORT)
	{
		const int comps = std::max(1, frame->nComponents);
		const uint16_t *src = reinterpret_cast<const uint16_t*>(frame->buf);
		for(int y = 0; y < frame->height; y += step)
		{
			for(int x = 0; x < frame->width; x += step)
			{
				const int idx = (y * frame->width + x) * comps;
				double r = 0.0, g = 0.0, b = 0.0;
				if(comps >= 3)
				{
					r = src[idx + 0] / 65535.0;
					g = src[idx + 1] / 65535.0;
					b = src[idx + 2] / 65535.0;
				}
				else
				{
					r = g = b = src[idx] / 65535.0;
				}
				addSample(r, g, b);
			}
		}
	}
	else if(frame->format == GL_UNSIGNED_INT_10_10_10_2)
	{
		const uint32_t *src = reinterpret_cast<const uint32_t*>(frame->buf);
		for(int y = 0; y < frame->height; y += step)
		{
			for(int x = 0; x < frame->width; x += step)
			{
				const uint32_t px = src[y * frame->width + x];
				// For luma/spread estimation channel order is not critical.
				double r = double(px & 0x3FFu) / 1023.0;
				double g = double((px >> 10) & 0x3FFu) / 1023.0;
				double b = double((px >> 20) & 0x3FFu) / 1023.0;
				addSample(r, g, b);
			}
		}
	}
	else
	{
		return false;
	}

	if(samples <= 0)
		return false;

	const int lowTarget = int(std::round(samples * 0.02));
	const int highTarget = int(std::round(samples * 0.98));
	int cumulative = 0;
	int lowBin = 0;
	int highBin = 255;
	for(int i = 0; i < 256; ++i)
	{
		cumulative += hist[size_t(i)];
		if(cumulative >= lowTarget)
		{
			lowBin = i;
			break;
		}
	}
	cumulative = 0;
	for(int i = 0; i < 256; ++i)
	{
		cumulative += hist[size_t(i)];
		if(cumulative >= highTarget)
		{
			highBin = i;
			break;
		}
	}

	const double rMean = sumR / samples;
	const double gMean = sumG / samples;
	const double bMean = sumB / samples;

	stats.meanLuma = sumLuma / samples;
	stats.lumaLow = lowBin / 255.0;
	stats.lumaHigh = std::max(stats.lumaLow + 0.01, highBin / 255.0);
	stats.channelSpread =
			(std::abs(rMean - gMean) +
			 std::abs(gMean - bMean) +
			 std::abs(bMean - rMean)) / 3.0;
	stats.valid = true;
	return true;
}

static QString BuildDarkThemeStyleSheet()
{
	return QString(
			"QWidget { background-color: #2b2b2b; color: #e8e8e8; }"
			"QMainWindow, QDialog { background-color: #2b2b2b; }"
			"QMenuBar { background-color: #323232; color: #e8e8e8; }"
			"QMenuBar::item:selected { background-color: #454545; }"
			"QMenu { background-color: #323232; color: #e8e8e8; border: 1px solid #555555; }"
			"QMenu::item:selected { background-color: #4a4a4a; }"
			"QPushButton { background-color: #3a3a3a; border: 1px solid #555555; "
				"padding: 4px; border-radius: 3px; }"
			"QPushButton:hover { background-color: #464646; }"
			"QPushButton:disabled { color: #888888; background-color: #2f2f2f; border-color: #3b3b3b; }"
			"QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTextEdit, QPlainTextEdit { "
				"background-color: #1f1f1f; color: #f0f0f0; border: 1px solid #555555; "
				"selection-background-color: #2d6ea3; }"
			"QTabWidget::pane { border: 1px solid #555555; }"
			"QTabBar::tab { background-color: #363636; color: #e8e8e8; "
				"padding: 6px; border: 1px solid #555555; border-bottom: none; }"
			"QTabBar::tab:selected { background-color: #4f8cc9; color: #ffffff; "
				"font-weight: 700; border-color: #74a6d8; }"
			"QStatusBar { background-color: #323232; color: #e8e8e8; }"
			"QToolTip { color: #ffffff; background-color: #444444; border: 1px solid #777777; }"
			"QProgressBar { border: 1px solid #555555; text-align: center; "
				"background-color: #1f1f1f; color: #e8e8e8; }"
			"QProgressBar::chunk { background-color: #4f8cc9; }"
			"QGroupBox::title { font-weight: 700; }");
}

static QString BuildLightThemeStyleSheet()
{
	return QString(
			"QTabBar::tab { background-color: #e6e6e6; color: #202020; "
				"padding: 6px; border: 1px solid #b8b8b8; border-bottom: none; }"
			"QTabBar::tab:selected { background-color: #2f5f9f; color: #ffffff; "
				"font-weight: 700; border-color: #4f78b4; }"
			"QGroupBox::title { font-weight: 700; }");
}

static int ClampRangeInt(int value, int minValue, int maxValue)
{
	if(value < minValue) return minValue;
	if(value > maxValue) return maxValue;
	return value;
}

static bool ConfirmExtractionCancel(QProgressDialog &progress,
		int currentValue, const QString &currentLabel)
{
	if(!progress.wasCanceled()) return false;

	int answer = QMessageBox::question(
			&progress,
			QObject::tr("Extractie onderbreken"),
			QObject::tr("Wil je de lopende extractie echt onderbreken?"),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
	if(answer == QMessageBox::Yes) return true;

	int minVal = progress.minimum();
	int maxVal = progress.maximum();
	progress.reset();
	progress.setRange(minVal, maxVal);
	progress.setValue(currentValue);
	progress.setLabelText(currentLabel);
	progress.show();
	qApp->processEvents();
	return false;
}

#ifndef USELIBAV
static QString FindExternalFfmpeg()
{
#ifdef Q_OS_WIN
	const QString bundled = QDir(qApp->applicationDirPath()).filePath("ffmpeg.exe");
	if(QFileInfo::exists(bundled))
		return bundled;
#endif
	return QStandardPaths::findExecutable("ffmpeg");
}

static void SaveTextureAsPngFrame(const FrameTexture *frame,
		const QString &frameDir, int frameIndex, const QRect &cropRect,
		bool flipHorizontal, bool flipVertical, int rotationDegrees)
{
	if(!frame || !frame->buf || frame->width <= 0 || frame->height <= 0)
		throw std::runtime_error("Video frame buffer is not initialized");

	QImage raw(reinterpret_cast<const uchar*>(frame->buf),
			frame->width, frame->height, QImage::Format_ARGB32);
	QImage out = raw;
	if(cropRect.width() > 0 && cropRect.height() > 0)
		out = out.copy(cropRect);
	if(flipHorizontal || flipVertical)
		out = out.mirrored(flipHorizontal, flipVertical);
	if(rotationDegrees != 0)
	{
		QTransform t;
		t.rotate(rotationDegrees);
		out = out.transformed(t, Qt::SmoothTransformation);
	}
	QString path = QDir(frameDir).filePath(
			QString("frame_%1.png").arg(frameIndex, 6, 10, QChar('0')));
	if(!out.save(path, "PNG"))
		throw std::runtime_error("Could not write temporary video frame image");
}

static QRect ComputeAlignedVideoCropRect(const FrameTexture *frame,
		const Frame_Window *frameWindow,
		float uiLeftNorm, float uiRightNorm,
		float uiTopNorm, float uiBottomNorm,
		bool useStableFrameCrop,
		float overlapDriftNorm)
{
	if(!frame || frame->width <= 1 || frame->height <= 1)
		return QRect();

	float leftNorm = std::min(uiLeftNorm, uiRightNorm);
	float rightNorm = std::max(uiLeftNorm, uiRightNorm);
	float staticTopNorm = uiTopNorm;
	float staticBottomNorm = 1.0f - uiBottomNorm;
	float topNorm = staticTopNorm;
	float bottomNorm = staticBottomNorm;

	auto clamp01 = [](float v) { return std::max(0.0f, std::min(1.0f, v)); };
	leftNorm = clamp01(leftNorm);
	rightNorm = clamp01(rightNorm);
	staticTopNorm = clamp01(staticTopNorm);
	staticBottomNorm = clamp01(staticBottomNorm);
	topNorm = staticTopNorm;
	bottomNorm = staticBottomNorm;

	if(frameWindow)
	{
		float fwLeft = std::min(frameWindow->pixbounds[0], frameWindow->pixbounds[1]);
		float fwRight = std::max(frameWindow->pixbounds[0], frameWindow->pixbounds[1]);
		if(std::abs(fwRight - fwLeft) > 0.01f)
		{
			leftNorm = clamp01(fwLeft);
			rightNorm = clamp01(fwRight);
		}

		if(!useStableFrameCrop)
		{
			// In adaptive mode, keep the user-defined crop height but shift
			// both lines together by the overlap drift derived from Search Size.
			float shiftedTop = clamp01(staticTopNorm + overlapDriftNorm);
			float shiftedBottom = clamp01(staticBottomNorm + overlapDriftNorm);
			topNorm = std::min(shiftedTop, shiftedBottom);
			bottomNorm = std::max(shiftedTop, shiftedBottom);
		}
	}

	if(bottomNorm < topNorm)
		std::swap(bottomNorm, topNorm);

	// Fallback to stable slider-based crop if dynamic values collapse.
	if((bottomNorm - topNorm) < 0.02f)
	{
		topNorm = std::min(staticTopNorm, staticBottomNorm);
		bottomNorm = std::max(staticTopNorm, staticBottomNorm);
	}

	int x1 = static_cast<int>(leftNorm * frame->width + 0.5f);
	int x2 = static_cast<int>(rightNorm * frame->width + 0.5f);
	int y1 = static_cast<int>(topNorm * frame->height + 0.5f);
	int y2 = static_cast<int>(bottomNorm * frame->height + 0.5f);

	x1 = std::max(0, std::min(frame->width - 1, x1));
	x2 = std::max(0, std::min(frame->width, x2));
	y1 = std::max(0, std::min(frame->height - 1, y1));
	y2 = std::max(0, std::min(frame->height, y2));

	int w = x2 - x1;
	int h = y2 - y1;
	if(w < 2)
	{
		x1 = std::max(0, std::min(frame->width - 2, x1));
		w = 2;
	}
	if(h < 2)
	{
		y1 = std::max(0, std::min(frame->height - 2, y1));
		h = 2;
	}

	if(w % 2) --w;
	if(h % 2) --h;
	if(w < 2) w = 2;
	if(h < 2) h = 2;

	return QRect(x1, y1, w, h);
}

static void EncodeVideoWithExternalFfmpeg(const QString &frameDir,
		const QString &audioWav, const QString &videoOut, double fps,
		int exportMode,
		int containerMode,
		int codecMode,
		int resolutionMode,
		int qualityPercent,
		int audioBitrateKbps,
		int presetMode,
		bool useAutoEq,
		double autoBrightness,
		double autoContrast,
		double autoSaturation,
		int totalFrames,
		QProgressDialog *progress, bool *wasCanceled,
		bool allowCancel)
{
	if(wasCanceled) *wasCanceled = false;

	QString ffmpegBin = FindExternalFfmpeg();
	if(ffmpegBin.isEmpty())
		throw std::runtime_error(
				"FFmpeg executable not found for video export. "
				"Install FFmpeg or place ffmpeg.exe next to AEO-Light.");

	int effectiveQuality = std::max(1, std::min(100, qualityPercent));
	int effectiveAudioBitrate = std::max(96, std::min(512, audioBitrateKbps));
	switch(exportMode)
	{
	case SIMPLE_EXPORT_FAST:
		effectiveQuality = std::min(effectiveQuality, 65);
		effectiveAudioBitrate = std::min(effectiveAudioBitrate, 128);
		break;
	case SIMPLE_EXPORT_QUALITY:
		effectiveQuality = std::max(effectiveQuality, 90);
		effectiveAudioBitrate = std::max(effectiveAudioBitrate, 192);
		break;
	case SIMPLE_EXPORT_ULTRA:
		effectiveQuality = std::max(effectiveQuality, 96);
		effectiveAudioBitrate = std::max(effectiveAudioBitrate, 256);
		break;
	default:
		break;
	}

	int crf = 34 - (effectiveQuality * 20 / 100); // 100% -> 14, 1% -> 34
	crf = std::max(14, std::min(34, crf));
	int mpeg4Q = 11 - (effectiveQuality / 10);    // 100% -> 1, ~50% -> 6
	mpeg4Q = std::max(2, std::min(10, mpeg4Q));

	QString containerArg = "mp4";
	switch(containerMode)
	{
	case VIDEO_CONTAINER_MOV: containerArg = "mov"; break;
	case VIDEO_CONTAINER_MKV: containerArg = "matroska"; break;
	default: break;
	}

	QString codecArg = "libx264";
	switch(codecMode)
	{
	case VIDEO_CODEC_HEVC: codecArg = "libx265"; break;
	case VIDEO_CODEC_MPEG4: codecArg = "mpeg4"; break;
	default: break;
	}

	QString presetArg = "medium";
	switch(presetMode)
	{
	case 0: presetArg = "veryfast"; break;
	case 2: presetArg = "slow"; break;
	default: break;
	}
	if(exportMode == SIMPLE_EXPORT_ULTRA && presetArg != "slow")
		presetArg = "slow";

	QString fpsArg = QString::number(fps, 'f', 3);
	QString pattern = QDir(frameDir).filePath("frame_%06d.png");
	QStringList args;
	args << "-y"
	     << "-framerate" << fpsArg
	     << "-i" << pattern
	     << "-i" << audioWav
	     << "-f" << containerArg;

	QStringList vfParts;
	QSize targetSize = VideoResolutionSizeForMode(resolutionMode);
	if(targetSize.width() > 0 && targetSize.height() > 0)
	{
		vfParts << QString(
				"scale=w=%1:h=%2:force_original_aspect_ratio=decrease:flags=lanczos,"
				"pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black")
				.arg(targetSize.width())
				.arg(targetSize.height());
	}
	if(useAutoEq)
	{
		QString eq = QString("eq=contrast=%1:brightness=%2")
				.arg(autoContrast, 0, 'f', 3)
				.arg(autoBrightness, 0, 'f', 3);
		if(std::abs(autoSaturation - 1.0) > 0.01)
			eq += QString(":saturation=%1").arg(autoSaturation, 0, 'f', 3);
		vfParts << eq;
	}
	if(!vfParts.isEmpty())
	{
		args << "-vf" << vfParts.join(",");
	}

	if(codecArg == "mpeg4")
	{
		args << "-c:v" << "mpeg4"
			 << "-q:v" << QString::number(mpeg4Q)
			 << "-pix_fmt" << "yuv420p";
	}
	else
	{
		args << "-c:v" << codecArg
			 << "-preset" << presetArg
			 << "-crf" << QString::number(crf)
			 << "-pix_fmt" << "yuv420p";
	}

	args << "-c:a" << "aac"
		 << "-b:a" << QString("%1k").arg(effectiveAudioBitrate)
		 << "-shortest"
	     << videoOut;

	QProcess ffmpeg;
	ffmpeg.setProcessChannelMode(QProcess::MergedChannels);
	ffmpeg.start(ffmpegBin, args);
	if(!ffmpeg.waitForStarted())
		throw std::runtime_error("Could not start ffmpeg process");

	QString ffmpegOutput;
	QRegularExpression frameRegex("frame=\\s*(\\d+)");
	int encodedFrames = 0;
	QElapsedTimer ffmpegTimer;
	ffmpegTimer.start();

	auto updateFfmpegProgress = [&]()
	{
		if(!progress) return;
		if(totalFrames > 0)
		{
			progress->setRange(0, totalFrames);
			progress->setValue(std::max(0, std::min(totalFrames, encodedFrames)));
		}

		const double elapsedSec = ffmpegTimer.elapsed() / 1000.0;
		QString status = QString("Video coderen... (%1/%2 frames)")
				.arg(std::max(0, encodedFrames))
				.arg(std::max(0, totalFrames));
		status += QString("\nVerstreken: %1")
				.arg(FormatEtaSeconds(elapsedSec));

		if(totalFrames > 0 && encodedFrames > 0)
		{
			const double fpsNow = encodedFrames / std::max(0.001, elapsedSec);
			const double remainingSec = (totalFrames - encodedFrames) /
					std::max(0.001, fpsNow);
			const double expectedTotalSec = elapsedSec + remainingSec;
			status += QString(" | Verwacht totaal: %1 | Resterend: %2")
					.arg(FormatEtaSeconds(expectedTotalSec))
					.arg(FormatEtaSeconds(remainingSec));
		}
		else
		{
			status += " | Verwacht totaal: --:-- | Resterend: --:--";
		}
		status += "\n(klik op Cancel om te onderbreken)";
		progress->setLabelText(status);
	};

	updateFfmpegProgress();

	while(ffmpeg.state() != QProcess::NotRunning)
	{
		ffmpeg.waitForFinished(100);
		ffmpegOutput += QString::fromLocal8Bit(ffmpeg.readAll());
		if(ffmpegOutput.size() > 32768)
			ffmpegOutput = ffmpegOutput.right(32768);
		auto matchIt = frameRegex.globalMatch(ffmpegOutput);
		while(matchIt.hasNext())
		{
			const QRegularExpressionMatch m = matchIt.next();
			encodedFrames = std::max(encodedFrames, m.captured(1).toInt());
		}
		updateFfmpegProgress();
		qApp->processEvents();
		if(allowCancel && progress && progress->wasCanceled())
		{
			if(!ConfirmExtractionCancel(
						*progress, progress->value(), progress->labelText()))
			{
				continue;
			}
			ffmpeg.terminate();
			if(!ffmpeg.waitForFinished(2000))
			{
				ffmpeg.kill();
				ffmpeg.waitForFinished(2000);
			}
			if(wasCanceled) *wasCanceled = true;
			return;
		}
	}

	ffmpegOutput += QString::fromLocal8Bit(ffmpeg.readAll());

	if(ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0)
	{
		throw std::runtime_error(
				QString("ffmpeg failed while creating video: %1").arg(ffmpegOutput).toStdString());
	}
}
#endif

extern "C" void SegvHandler(int param);
static jmp_buf segvJumpEnv;
static long lastFrameLoad = 0;
static const char *traceCurrentOperation = NULL;
static const char *traceSubroutineOperation = NULL;

#ifndef UMAX
	#define UMAX(b) ((1ull<<(b))-1)
#endif
#ifndef SMAX
	#define SMAX(b) (UMAX((b)-1))
#endif

//----------------------------------------------------------------------------
ExtractedSound::ExtractedSound() :
	frameIn(0),
	frameOut(0),
	videoStableCrop(true),
	videoDriftSensitivity(100),
	gamma(100),
	gain (100),
	sCurve(0),
	overlap(20),
	lift(0),
	blur(0),
    fpsType(FPS_24),
    rot_angle(0),
    useBounds(true),
    usePixBounds(false),
    useSCurve(false),
    makeNegative(false),
    makeGray(false),
    applyRotation(false),
	sound(NULL),
	err(0)
{
	bounds[0] = bounds[1] = 0;
	pixBounds[0] = pixBounds[1] = 0;
	framePitch[0] = framePitch[1] = 0;
	videoFrameCrop[0] = videoFrameCrop[1] = 100;
}

ExtractedSound::~ExtractedSound()
{
	//let the deque handle this: too many copies being made to
	// handle here. We'd need to switch to a smart pointer
#if 0
	if(sound != NULL)
	{
		sound->stop();
		QFile::remove(sound->fileName());
	}
#endif
}

bool ExtractedSound::operator ==(const ExtractedSound &ref) const
{
	if(frameIn != ref.frameIn) return false;
	if(frameOut != ref.frameOut) return false;
	if(framePitch[0] != ref.framePitch[0]) return false;
	if(framePitch[1] != ref.framePitch[1]) return false;
	if(videoFrameCrop[0] != ref.videoFrameCrop[0]) return false;
	if(videoFrameCrop[1] != ref.videoFrameCrop[1]) return false;
	if(videoStableCrop != ref.videoStableCrop) return false;
	if(videoDriftSensitivity != ref.videoDriftSensitivity) return false;
	if(overlap != ref.overlap) return false;

	if(useBounds != ref.useBounds) return false;
	if(usePixBounds != ref.usePixBounds) return false;
	if(useBounds &&
			((bounds[0] != ref.bounds[0]) || (bounds[1] != ref.bounds[1])))
		return false;
	if(usePixBounds &&
			((pixBounds[0] != ref.pixBounds[0]) ||
			(pixBounds[1] != ref.pixBounds[1])))
		return false;

	if(gamma != ref.gamma) return false;
	if(gain != ref.gain) return false;
	if(lift != ref.lift) return false;
	if(blur != ref.blur) return false;

	if(useSCurve != ref.useSCurve) return false;
	if(useSCurve && (sCurve != ref.sCurve)) return false;

	if(fpsType != ref.fpsType) return false;

	if(makeNegative != ref.makeNegative) return false;
	if(makeGray != ref.makeGray) return false;

	return true;
}

//----------------------------------------------------------------------------

#define TRANSSLIDER_VALUE 200


QString MainWindow::Version()
{
	// Version number is set in the .pro file
	return QCoreApplication::applicationName() + QString(" v") +
			QCoreApplication::applicationVersion() /* + " (" __DATE__ ")"*/;
}

void RecursivelyEnable(QLayout *l, bool enable=true)
{
	int i;
	QLayoutItem *item;

	for(i=0; i<l->count(); ++i)
	{
		item = l->itemAt(i);
		if(item)
		{
			if(item->widget()) item->widget()->setEnabled(enable);
			else if(item->layout()) RecursivelyEnable(item->layout(), enable);
		}
	}
}

// -------------------------------------------------------------------
// Constructor
// -------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	ApplyThemeFromSettings();

    #ifdef Q_OS_WIN
	QString fontsize = "18pt";
	#else
	QString fontsize = "24pt";
	#endif

	ui->actionAbout->setMenuRole(QAction::AboutRole);
	ui->actionQuit->setMenuRole(QAction::QuitRole);
	ui->actionPreferences->setMenuRole(QAction::PreferencesRole);

	#ifdef AVAILABLE_MAC_OS_X_VERSION_10_11_AND_LATER
	// As of 10.11 (El Capitan), OSX automagically adds "Enter Full Screen"
	// to any menu named "View", so we have to name it something else in
	// order to avoid this undesired behavior.
	// Here we append a zero-width space character.
	ui->menuView->setTitle("View\u200C");
	#endif

	ui->appNameLabel->setText(
			QString("<html><head/><body><p><span style=\" font-size:"
			+ fontsize + ";\">")
            + Version() + QString("</span></p></body></html>"));

	ui->FramePitchendSlider->setValue(this->scan.overlapThreshold);

	ui->maxFrequencyFrame->setVisible(false);
	ui->tabWidget->setCurrentIndex(0);
	//ui->soundtrackSettingGrid->setRowStretch(0,1);
	frame_window = NULL;
	paramCopyLock = false;
	samplesPlayed.resize(4);
	currentMeta = NULL;
	currentFrameTexture = NULL;
	outputFrameTexture = NULL;

	// turn off stuff that can't be used until a project is loaded
	ui->saveprojectButton->setEnabled(false);
	ui->actionSave_Settings->setEnabled(false);
	ui->actionShow_Overlap->setEnabled(false);
	ui->actionShow_Soundtrack_Only->setEnabled(false);
	ui->actionWaveform_Zoom->setEnabled(false);
	ui->loadSettingsButton->setEnabled(false);
	ui->loadLastSettingsButton->setEnabled(false);
	ui->tabWidget->setEnabled(false);
	RecursivelyEnable(ui->viewOptionsLayout, false);
	RecursivelyEnable(ui->frameNumberLayout, false);
	RecursivelyEnable(ui->sampleLayout, false);
    UpdateQueueWidgets();

    // turn of fthe XML sidecard stuff
    ui->xmlSidecarComboBox->setVisible(false);
    ui->xmlSidecarLabel->setVisible(false);

    ui->filmrate_PD->setCurrentIndex(1); // default to 24fps
    ui->degreeSpinBox->setSingleStep(0.01);
	// skip license agreement if disabled by preference, otherwise keep
	// the existing per-version acceptance behavior.
	QSettings settings;
	settings.beginGroup("ui");
	const bool skipLicenseAgreement =
			settings.value("skip-license-agreement", false).toBool();
	settings.endGroup();
	QString ag = settings.value("license","0.1").toString();
	QVersionNumber agv = QVersionNumber::fromString(ag);
	QVersionNumber thisv = QVersionNumber::fromString(APP_VERSION);


	Log() << "Starting project: " << startingProjectFilename << "\n";

	if(!skipLicenseAgreement && QVersionNumber::compare(agv, thisv) < 0)
		QTimer::singleShot(0, this, SLOT(LicenseAgreement()));
	else
		QTimer::singleShot(0, this, SLOT(OpenStartingProject()));

	prevProjectDir = "";

	// start log with timestamp
	Log() << QDateTime::currentDateTime().toString() << "\n";

	isVideoMuxingRisky = false;
	videoExportMode = SIMPLE_EXPORT_STANDARD;
	videoTargetProfileMode = VIDEO_TARGET_PROFILE_CUSTOM;
	videoContainerMode = VIDEO_CONTAINER_MP4;
	videoCodecMode = VIDEO_CODEC_H264;
	videoResolutionMode = VIDEO_RESOLUTION_SOURCE;
	videoQualityPercent = 85;
	videoAudioBitrateKbps = 192;
	videoPresetMode = 1;
	outputConflictMode = 0;
	outputOpenFolderAfterExport = false;
	autoCleanupTempFilesEnabled = true;
	exportReportMode = EXPORT_REPORT_OFF;
	queueBatchReportMode = QUEUE_BATCH_REPORT_OFF;
	videoAudioOffsetCorrectionEnabled = false;
	videoUseStableFrameCrop = true;
	videoDriftSensitivityFactor = 1.0f;
	videoFlipHorizontal = false;
	videoFlipVertical = false;
	videoRotationDegrees = 0;
	pitchControlSyncLock = false;
	videoCropControlSyncLock = false;
	skipCloseConfirmation = false;
	applyingWindowLayout = false;
	audioQualityPresetSyncLock = false;
	audioOnlyQualityPresetSyncLock = false;
	queueBatchRunning = false;
	queueBatchPaused = false;
	queueBatchSkipRequested = false;
	lastExtractFailureReason.clear();
	autoOptimizeFramesEnabled = false;
	autoOptimizeVideoEnabled = false;
	autoOptimizeColorEnabled = true;
	autoOptimizeProfileMode = 1;
	compactModeEnabled = false;
	autoOptimizeFrameApplyLock = false;
	suppressFrameAutoOptimizeDuringExtraction = false;
	uiScalePercent = -1;
	uiScaleGeometryCaptured = false;
	uiScaleLayoutActive = false;
	uiScaleBaseSize = QSize();
	LoadVideoExportSettings();
	LoadExtractOutputSettings();
	InstallInlineHelpIcons();
	bool usedLegacyShowHelpKey = false;
	const bool showHelpText = ReadShowHelpTextSetting(&usedLegacyShowHelpKey);
	UpdateHelpTextVisibility(showHelpText, usedLegacyShowHelpKey);
	ApplyCompactModePreference();

	CaptureUiScaleBaseGeometry();
	RestoreUiWindowSettings();
	UpdateAdaptiveUiScale();

	#ifdef USE_MUX_HACK
	encStartFrame = 0;
	encNumFrames = 0;
	encCurFrame = 0;

	encVideoBufSize = 0;

	encRGBFrame = NULL;
	encS16Frame = NULL;

	encAudioLen = 0;
	encAudioNextPts = 0;
	#endif

    connect(ui->queueLoad1Button, &QPushButton::clicked,
            this, [this](){ this->QueueLoadSettings(0); });
    connect(ui->queueLoad2Button, &QPushButton::clicked,
            this, [this](){ this->QueueLoadSettings(1); });
    connect(ui->queueLoad3Button, &QPushButton::clicked,
            this, [this](){ this->QueueLoadSettings(2); });
    connect(ui->queueLoad4Button, &QPushButton::clicked,
            this, [this](){ this->QueueLoadSettings(3); });
    connect(ui->queueLoad5Button, &QPushButton::clicked,
            this, [this](){ this->QueueLoadSettings(4); });

    connect(ui->queueUpdate1Button, &QPushButton::clicked,
            this, [this](){ this->QueueUpdateSettings(0); });
    connect(ui->queueUpdate2Button, &QPushButton::clicked,
            this, [this](){ this->QueueUpdateSettings(1); });
    connect(ui->queueUpdate3Button, &QPushButton::clicked,
            this, [this](){ this->QueueUpdateSettings(2); });
    connect(ui->queueUpdate4Button, &QPushButton::clicked,
            this, [this](){ this->QueueUpdateSettings(3); });
    connect(ui->queueUpdate5Button, &QPushButton::clicked,
            this, [this](){ this->QueueUpdateSettings(4); });

	connect(ui->queueDuplicate1Button, &QPushButton::clicked,
			this, [this](){ this->QueueDuplicateSettings(0); });
	connect(ui->queueDuplicate2Button, &QPushButton::clicked,
			this, [this](){ this->QueueDuplicateSettings(1); });
	connect(ui->queueDuplicate3Button, &QPushButton::clicked,
			this, [this](){ this->QueueDuplicateSettings(2); });
	connect(ui->queueDuplicate4Button, &QPushButton::clicked,
			this, [this](){ this->QueueDuplicateSettings(3); });
	connect(ui->queueDuplicate5Button, &QPushButton::clicked,
			this, [this](){ this->QueueDuplicateSettings(4); });

	connect(ui->queueUp1Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsUp(0); });
	connect(ui->queueUp2Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsUp(1); });
	connect(ui->queueUp3Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsUp(2); });
	connect(ui->queueUp4Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsUp(3); });
	connect(ui->queueUp5Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsUp(4); });

	connect(ui->queueDown1Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsDown(0); });
	connect(ui->queueDown2Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsDown(1); });
	connect(ui->queueDown3Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsDown(2); });
	connect(ui->queueDown4Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsDown(3); });
	connect(ui->queueDown5Button, &QPushButton::clicked,
			this, [this](){ this->QueueMoveSettingsDown(4); });

    connect(ui->queueImportVFBClipsButton, &QPushButton::clicked,
            this, &MainWindow::QueueImportVFB);
	connect(ui->queueRetryFailedButton, &QPushButton::clicked,
			this, [this]() { this->RetryFailedQueueItems(); });

	// Quick-access shortcuts for the most-used extraction actions.
	QShortcut *extractShortcut = new QShortcut(QKeySequence("Ctrl+E"), this);
	connect(extractShortcut, &QShortcut::activated, this, [this]()
	{
		if(ui->extractGLButton->isEnabled())
			ui->extractGLButton->click();
	});
	QShortcut *testRunShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
	connect(testRunShortcut, &QShortcut::activated, this, [this]()
	{
		if(ui->testRunExportButton->isEnabled())
			ui->testRunExportButton->click();
	});
	QShortcut *enqueueShortcut = new QShortcut(QKeySequence("Ctrl+Shift+A"), this);
	connect(enqueueShortcut, &QShortcut::activated, this, [this]()
	{
		if(ui->enqueueButton->isEnabled())
			ui->enqueueButton->click();
	});
	QShortcut *lastSettingsShortcut = new QShortcut(QKeySequence("Ctrl+L"), this);
	connect(lastSettingsShortcut, &QShortcut::activated, this, [this]()
	{
		if(ui->loadLastSettingsButton->isEnabled())
			ui->loadLastSettingsButton->click();
	});

	ui->extractGLButton->setToolTip(
			tr("Start extractie (Ctrl+E)"));
	ui->testRunExportButton->setToolTip(
			tr("Korte test-export (Ctrl+T)"));
	ui->enqueueButton->setToolTip(
			tr("Voeg huidige instellingen toe aan queue (Ctrl+Shift+A)"));
	ui->loadLastSettingsButton->setToolTip(
			tr("Laad laatst gebruikte instellingen (Ctrl+L)"));
	UpdateVideoStabilizationControls();
}

MainWindow::~MainWindow()
{
	DeleteTempSoundFile();
	delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	auto closePreviewWindow = [this]() {
		if(frame_window)
			frame_window->close();
	};

	if(skipCloseConfirmation)
	{
		SaveUiWindowSettings();
		closePreviewWindow();
		event->accept();
		return;
	}

	QSettings settings;
	settings.beginGroup("ui");
	bool skipConfirmByPreference =
			settings.value("skip-close-confirmation", false).toBool();
	settings.endGroup();
	if(skipConfirmByPreference)
	{
		SaveUiWindowSettings();
		closePreviewWindow();
		event->accept();
		return;
	}

	int answer = QMessageBox::question(
			this,
			tr("AEO-Light afsluiten"),
			tr("Wil je AEO-Light echt afsluiten?"),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
	if(answer == QMessageBox::Yes)
	{
		SaveUiWindowSettings();
		closePreviewWindow();
		event->accept();
	}
	else
		event->ignore();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
	QMainWindow::moveEvent(event);
	PositionPreviewWindowBesideMainWindow();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
	QMainWindow::resizeEvent(event);
	UpdateAdaptiveUiScale();
	PositionPreviewWindowBesideMainWindow();
}

void MainWindow::CaptureUiScaleBaseGeometry()
{
	QWidget *cw = this->centralWidget();
	if(!cw) return;

	uiScaleBaseSize = cw->size();
	if(uiScaleBaseSize.width() <= 0 || uiScaleBaseSize.height() <= 0)
		uiScaleBaseSize = QSize(760, 640);

	uiScaleBaseGeometry.clear();
	uiScaleBaseFontPt.clear();

	const QList<QWidget*> topChildren = cw->findChildren<QWidget*>(
			QString(), Qt::FindDirectChildrenOnly);
	for(QWidget *w : topChildren)
	{
		if(!w) continue;
		uiScaleBaseGeometry.insert(w, w->geometry());
	}

	QList<QWidget*> allChildren = cw->findChildren<QWidget*>();
	allChildren.prepend(cw);
	for(QWidget *w : allChildren)
	{
		if(!w) continue;
		int pt = w->font().pointSize();
		if(pt > 0)
			uiScaleBaseFontPt.insert(w, pt);
	}

	uiScaleGeometryCaptured = true;
}

void MainWindow::UpdateAdaptiveUiScale()
{
	QWidget *cw = this->centralWidget();
	if(!cw) return;
	if(!uiScaleGeometryCaptured)
		CaptureUiScaleBaseGeometry();
	if(!uiScaleGeometryCaptured)
		return;

	// Requested fallback: keep the legacy fixed "standard" UI layout.
	// We reset geometry/fonts/styles to the baseline captured from .ui.
	for(auto it = uiScaleBaseGeometry.constBegin(); it != uiScaleBaseGeometry.constEnd(); ++it)
	{
		QWidget *w = it.key();
		if(!w) continue;
		w->setGeometry(it.value());
	}

	for(auto it = uiScaleBaseFontPt.constBegin(); it != uiScaleBaseFontPt.constEnd(); ++it)
	{
		QWidget *w = it.key();
		if(!w) continue;
		QFont f = w->font();
		f.setPointSize(it.value());
		w->setFont(f);
	}

	const QList<QSlider*> sliders = cw->findChildren<QSlider*>();
	for(QSlider *slider : sliders)
	{
		if(!slider) continue;
		slider->setStyleSheet(QString());
	}

	uiScalePercent = 100;
	uiScaleLayoutActive = false;
}

void MainWindow::RestoreUiWindowSettings()
{
	QSettings settings;
	settings.beginGroup("ui");
	const QByteArray geometry = settings.value("main-window-geometry").toByteArray();
	const bool wasMaximized = settings.value("main-window-maximized", false).toBool();
	const bool wasFullscreen = settings.value("main-window-fullscreen", false).toBool();
	settings.endGroup();

	if(!geometry.isEmpty())
		restoreGeometry(geometry);

	if(wasFullscreen)
		setWindowState(windowState() | Qt::WindowFullScreen);
	else if(wasMaximized)
		setWindowState(windowState() | Qt::WindowMaximized);
}

void MainWindow::SaveUiWindowSettings() const
{
	QSettings settings;
	settings.beginGroup("ui");
	settings.setValue("main-window-geometry", saveGeometry());
	settings.setValue("main-window-maximized", isMaximized());
	settings.setValue("main-window-fullscreen", isFullScreen());
	settings.endGroup();
}

void MainWindow::PositionPreviewWindowBesideMainWindow()
{
	if(!frame_window) return;
	if(!frame_window->isVisible()) return;
	if(applyingWindowLayout) return;

	applyingWindowLayout = true;

	const QMargins previewMargins = frame_window->frameMargins();
	QSize previewContentSize = frame_window->size();
	if(previewContentSize.width() <= 0 || previewContentSize.height() <= 0)
		previewContentSize = QSize(640, 640);
	const double aspectRatio = std::max(0.1,
			static_cast<double>(previewContentSize.width()) /
			static_cast<double>(previewContentSize.height()));
	const int frameMarginsW = previewMargins.left() + previewMargins.right();
	const int frameMarginsH = previewMargins.top() + previewMargins.bottom();

	auto placePreviewInArea = [&](const QRect &area)
	{
		if(area.width() < 160 || area.height() < 160)
			return;

		int maxContentW = std::max(120, area.width() - frameMarginsW);
		int maxContentH = std::max(120, area.height() - frameMarginsH);
		int targetContentW = maxContentW;
		int targetContentH = std::max(120,
				static_cast<int>(std::lround(targetContentW / aspectRatio)));
		if(targetContentH > maxContentH)
		{
			targetContentH = maxContentH;
			targetContentW = std::max(120,
					static_cast<int>(std::lround(targetContentH * aspectRatio)));
		}
		targetContentW = std::min(targetContentW, maxContentW);
		targetContentH = std::min(targetContentH, maxContentH);

		if(frame_window->size() != QSize(targetContentW, targetContentH))
			frame_window->resize(targetContentW, targetContentH);

		const int targetFrameW = targetContentW + frameMarginsW;
		const int targetFrameH = targetContentH + frameMarginsH;
		int frameX = area.left() + (area.width() - targetFrameW) / 2;
		int frameY = area.top() + (area.height() - targetFrameH) / 2;

		const int contentX = frameX + previewMargins.left();
		const int contentY = frameY + previewMargins.top();
		frame_window->setPosition(contentX, contentY);
	};

	QSettings settings;
	settings.beginGroup("ui");
	QString layoutMode =
			settings.value("window-layout-mode", "").toString().toLower().trimmed();
	if(layoutMode.isEmpty())
	{
		const bool legacyDual =
				settings.value("window-layout-dual-screen", false).toBool();
		const bool legacySingle =
				settings.value("window-layout-single-fill", false).toBool();
		if(legacyDual)
			layoutMode = "dual";
		else if(legacySingle)
			layoutMode = "single";
		else
			layoutMode = "standard";
	}
	const bool dualSwap =
			settings.value("window-layout-dual-swap", false).toBool();
	const int targetMonitorIndex =
			settings.value("window-layout-target-monitor", 0).toInt();
	settings.endGroup();

	const QList<QScreen*> screens = QGuiApplication::screens();
	if(layoutMode == "dual" && screens.size() >= 2)
	{
		int mainScreenIndex = dualSwap ? 1 : 0;
		int previewScreenIndex = dualSwap ? 0 : 1;
		mainScreenIndex = std::max(0, std::min(mainScreenIndex, screens.size() - 1));
		previewScreenIndex = std::max(0, std::min(previewScreenIndex, screens.size() - 1));
		if(mainScreenIndex == previewScreenIndex)
			previewScreenIndex = (mainScreenIndex + 1) % screens.size();

		const QRect mainArea = screens.at(mainScreenIndex)->availableGeometry();
		const QRect previewArea = screens.at(previewScreenIndex)->availableGeometry();
		if(this->geometry() != mainArea)
			this->setGeometry(mainArea);
		placePreviewInArea(previewArea);
		applyingWindowLayout = false;
		return;
	}

	if(layoutMode == "single" && !screens.isEmpty())
	{
		const int monitorIndex = std::max(
				0, std::min(targetMonitorIndex, screens.size() - 1));
		const QRect available = screens.at(monitorIndex)->availableGeometry();
		const int splitGap = 10;
		// Keep the UI readable on large displays: reserve a meaningful
		// fraction of the monitor width for controls in split mode.
		const int minMainWidth = std::max(560, available.width() / 3);
		const int minFrameHeight = std::min(240, available.height());

		auto previewFrameWidthForHeight = [&](int frameHeight)
		{
			int contentHeight = std::max(120, frameHeight - frameMarginsH);
			int contentWidth = std::max(120,
					static_cast<int>(std::lround(contentHeight * aspectRatio)));
			return contentWidth + frameMarginsW;
		};

		int commonFrameHeight = available.height();
		int previewFrameWidth = previewFrameWidthForHeight(commonFrameHeight);
		int mainWidth = available.width() - splitGap - previewFrameWidth;

		if(mainWidth < minMainWidth)
		{
			double maxContentHeightByWidth =
					(static_cast<double>(available.width() - splitGap - minMainWidth - frameMarginsW) /
					std::max(0.1, aspectRatio));
			int maxFrameHeightByWidth = static_cast<int>(std::floor(maxContentHeightByWidth)) + frameMarginsH;
			commonFrameHeight = std::max(minFrameHeight,
					std::min(commonFrameHeight, maxFrameHeightByWidth));
			previewFrameWidth = previewFrameWidthForHeight(commonFrameHeight);
			mainWidth = available.width() - splitGap - previewFrameWidth;
		}

		if(mainWidth < 280)
			mainWidth = 280;

		int totalWidth = mainWidth + splitGap + previewFrameWidth;
		if(totalWidth > available.width())
		{
			mainWidth = std::max(280, available.width() - splitGap - previewFrameWidth);
			totalWidth = mainWidth + splitGap + previewFrameWidth;
		}

		const int startX = available.left() + std::max(0, (available.width() - totalWidth) / 2);
		const int startY = available.top() + std::max(0, (available.height() - commonFrameHeight) / 2);
		const QRect mainArea(startX, startY, mainWidth, commonFrameHeight);
		const QRect previewArea(
				mainArea.right() + 1 + splitGap,
				startY,
				previewFrameWidth,
				commonFrameHeight);
		if(this->geometry() != mainArea)
			this->setGeometry(mainArea);
		placePreviewInArea(previewArea);
		applyingWindowLayout = false;
		return;
	}

	const int gap = 12;
	const QRect mainRect = this->frameGeometry();

	QScreen *screen = this->screen();
	if(!screen) screen = QGuiApplication::primaryScreen();
	if(!screen)
	{
		applyingWindowLayout = false;
		return;
	}
	const QRect available = screen->availableGeometry();

	const int topAlignedFrameY = std::max(available.top(),
			std::min(mainRect.top(), available.bottom()));
	const int maxFrameHeightFromTop = std::max(240,
			available.bottom() - topAlignedFrameY + 1);
	int targetFrameHeight = std::min(mainRect.height(), maxFrameHeightFromTop);
	targetFrameHeight = std::max(targetFrameHeight, 240);
	int targetContentHeight = std::max(120, targetFrameHeight - frameMarginsH);
	int targetContentWidth = std::max(120,
			static_cast<int>(std::lround(targetContentHeight * aspectRatio)));
	int targetFrameWidth = targetContentWidth + frameMarginsW;

	const int rightSpace = std::max(120, available.right() - (mainRect.right() + gap) + 1);
	const int leftSpace = std::max(120, (mainRect.left() - gap) - available.left() + 1);
	bool placeRight = (rightSpace >= leftSpace);

	int availableSideWidth = placeRight ? rightSpace : leftSpace;
	if(targetFrameWidth > availableSideWidth)
	{
		targetFrameWidth = availableSideWidth;
		targetContentWidth = std::max(120, targetFrameWidth - frameMarginsW);
		targetContentHeight = std::max(120, static_cast<int>(
				std::lround(targetContentWidth / aspectRatio)));
		targetFrameHeight = targetContentHeight + frameMarginsH;
	}
	if(targetFrameHeight > maxFrameHeightFromTop)
	{
		targetFrameHeight = maxFrameHeightFromTop;
		targetContentHeight = std::max(120, targetFrameHeight - frameMarginsH);
		targetContentWidth = std::max(120, static_cast<int>(
				std::lround(targetContentHeight * aspectRatio)));
		targetFrameWidth = targetContentWidth + frameMarginsW;
	}

	if(frame_window->size() != QSize(targetContentWidth, targetContentHeight))
		frame_window->resize(targetContentWidth, targetContentHeight);

	int frameX = placeRight ? (mainRect.right() + gap) :
			(mainRect.left() - gap - targetFrameWidth);
	int frameY = topAlignedFrameY;

	if(frameX < available.left())
		frameX = available.left();
	if(frameX + targetFrameWidth > available.right())
		frameX = available.right() - targetFrameWidth + 1;

	if(frameY + targetFrameHeight > available.bottom())
		frameY = available.bottom() - targetFrameHeight + 1;
	if(frameY < available.top())
		frameY = available.top();

	// setPosition() is content-based for QWindow: compensate frame margins
	// so outer window frames end up aligned at equal height.
	const int contentX = frameX + previewMargins.left();
	const int contentY = frameY + previewMargins.top();
	frame_window->setPosition(contentX, contentY);
	applyingWindowLayout = false;
}

void MainWindow::SaveDefaultsSoundtrack()
{
	QSettings settings;

	if(!scan.inFile.IsReady()) return;

	double w = scan.inFile.Width();
	double h = scan.inFile.Height();

	settings.beginGroup("soundtrack");
	settings.setValue("bounds/left", ui->leftSpinBox->value()/w);
	settings.setValue("bounds/right", ui->rightSpinBox->value()/w);
	settings.setValue("bounds/use", ui->OverlapSoundtrackCheckBox->isChecked());
	settings.setValue("pixbounds/left", ui->leftPixSpinBox->value()/w);
	settings.setValue("pixbounds/right", ui->rightPixSpinBox->value()/w);
	settings.setValue("pixbounds/use", ui->OverlapPixCheckBox->isChecked());
	settings.setValue("framepitch/start", ui->framepitchstartSlider->value()/h);
	settings.setValue("framepitch/end", ui->FramePitchendSlider->value()/h);
	settings.setValue("videoframe/top", ui->videoFrameTopSlider->value()/1000.0);
	settings.setValue("videoframe/bottom", ui->videoFrameBottomSlider->value()/1000.0);
	settings.setValue("video/stable-crop", ui->videoStableCropCheckBox->isChecked());
	settings.setValue("video/drift-sensitivity", ui->videoDriftSensitivitySlider->value());
	settings.setValue("overlap/radius", ui->overlapSlider->value());
	settings.setValue("overlap/lock", ui->HeightCalculateBtn->isChecked());
	settings.setValue("isstereo", ui->monostereo_PD->currentIndex());
	settings.endGroup();
}

void MainWindow::SaveDefaultsImage()
{
	QSettings settings;

	settings.beginGroup("image");
	settings.setValue("lift", ui->liftSlider->value());
	settings.setValue("gamma", ui->gammaSlider->value());
	settings.setValue("gain", ui->gainSlider->value());
	settings.setValue("s-curve", ui->thresholdSlider->value());
	settings.setValue("s-curve-on", ui->threshBox->isChecked());
	settings.setValue("blur-sharp", ui->blurSlider->value());
	settings.setValue("negative", ui->negBox->isChecked());
	settings.setValue("desaturate",ui->desatBox->isChecked());
	settings.endGroup();
}

void MainWindow::SaveDefaultsAudio()
{
	QSettings settings;

	settings.beginGroup("extraction");
	settings.setValue("audio-quality-preset",
			ui->audioQualityPresetComboBox->currentIndex());
	settings.setValue("audio-only-quality-preset",
			ui->audioOnlyQualityPresetComboBox->currentIndex());
	settings.setValue("sampling-rate", ui->filerate_PD->currentText());
	settings.setValue("bit-depth", ui->bitDepthComboBox->currentText());
	settings.setValue("time-code-advance", ui->advance_CB->currentText());
	settings.setValue("sidecar", ui->xmlSidecarComboBox->currentText());
	settings.setValue("output-audio-file", ui->audioOutputFilenameEdit->text());
	settings.setValue("output-video-enabled", ui->outputVideoCheckBox->isChecked());
	settings.setValue("output-video-file", ui->videoOutputFilenameEdit->text());
	settings.setValue("output-name-template", ui->outputFilenameTemplateEdit->text());
	settings.setValue("output-conflict-mode", ui->outputConflictModeComboBox->currentIndex());
	settings.setValue("output-open-folder", ui->outputOpenFolderCheckBox->isChecked());
	settings.setValue("video-av-offset", ui->videoAvOffsetCheckBox->isChecked());
	settings.setValue("video-flip-h", ui->outputFlipHorizontalCheckBox->isChecked());
	settings.setValue("video-flip-v", ui->outputFlipVerticalCheckBox->isChecked());
	settings.setValue("video-rotation-index", ui->outputRotationComboBox->currentIndex());
	settings.endGroup();
}

void MainWindow::LoadDefaults()
{
	QSettings settings;
	int intv;
	double doublev;

	if(!scan.inFile.IsReady()) return;

	int w = scan.inFile.Width();
	int h = scan.inFile.Height();

	// Soundtrack Settings
	settings.beginGroup("soundtrack");

	intv = int(settings.value("bounds/left",0).toDouble() * w + 0.5);
	ui->leftSpinBox->setValue(intv);
	ui->leftSlider->setValue(intv);
	intv = int(settings.value("bounds/right",0).toDouble() * w + 0.5);
	ui->rightSpinBox->setValue(intv);
	ui->rightSlider->setValue(intv);
	ui->OverlapSoundtrackCheckBox->setChecked(
			settings.value("bounds/use",true).toBool());

	intv = int(settings.value("pixbounds/left",0.475).toDouble() * w + 0.5);
	ui->leftPixSpinBox->setValue(intv);
	ui->leftPixSlider->setValue(intv);
	intv = int(settings.value("pixbounds/right",0.525).toDouble() * w + 0.5);
	ui->rightPixSpinBox->setValue(intv);
	ui->rightPixSlider->setValue(intv);
	ui->OverlapPixCheckBox->setChecked(
			settings.value("pixbounds/use", false).toBool());
	on_OverlapPixCheckBox_clicked(ui->OverlapPixCheckBox->isChecked());

	intv = int(settings.value("framepitch/start", 0.1).toDouble() * h + 0.5);
	ui->framepitchstartSlider->setValue(intv);
	ui->framepitchstartLabel->setText(QString::number(intv/1000.f,'f',3));
	intv = int(settings.value("framepitch/end", 0.1).toDouble() * h + 0.5);
	ui->FramePitchendSlider->setValue(intv);
	ui->framePitchLabel->setText(QString::number(intv/1000.f,'f',3));

	intv = int(settings.value("videoframe/top", 0.1).toDouble() * 1000 + 0.5);
	ui->videoFrameTopSlider->setValue(intv);
	intv = int(settings.value("videoframe/bottom", 0.1).toDouble() * 1000 + 0.5);
	ui->videoFrameBottomSlider->setValue(intv);
	NormalizeVideoCropControls(true);
	UpdateVideoCropLabels();
	ui->videoStableCropCheckBox->setChecked(
			settings.value("video/stable-crop", true).toBool());
	intv = settings.value("video/drift-sensitivity", 100).toInt();
	ui->videoDriftSensitivitySlider->setValue(intv);
	UpdateVideoStabilizationControls();

	intv = settings.value("overlap/radius",5).toInt();
	ui->overlapSlider->setValue(intv);
	ui->overlap_label->setText(QString::number(intv/100.f,'f',2));

	ui->HeightCalculateBtn->setChecked(settings.value("overlap/lock").toBool());
	ui->monostereo_PD->setCurrentIndex(settings.value("isstereo",0).toInt());

	settings.endGroup();

	// image processing settings
	settings.beginGroup("image");

	intv = settings.value("lift",0).toInt();
	ui->liftSlider->setValue(intv);
	ui->liftLabel->setText(QString::number(intv/100.f,'f',2));

	intv = settings.value("gamma",100).toInt();
	ui->gammaSlider->setValue(intv);
	ui->gammaLabel->setText(QString::number(intv/100.f,'f',2));

	intv = settings.value("gain", 100).toInt();
	ui->gainSlider->setValue(intv);
	ui->gainLabel->setText(QString::number(intv/100.f,'f',2));

	intv = settings.value("s-curve",300).toInt();
	ui->thresholdSlider->setValue(intv);
	ui->threshLabel->setText(QString::number(intv/100.f,'f',2));

	ui->threshBox->setChecked(settings.value("s-curve-on",false).toBool());

	intv = settings.value("blur-sharp", 0).toInt();
	ui->blurSlider->setValue(intv);
	ui->blurLabel->setText(QString::number(intv/100.f,'f',2));

	ui->negBox->setChecked(settings.value("negative",false).toBool());
	ui->desatBox->setChecked(settings.value("desaturate",false).toBool());
	settings.endGroup();

	// extraction/export parameters

	settings.beginGroup("extraction");
	QString txt;

	txt = settings.value("sampling-rate").toString();
	intv = ui->filerate_PD->findText(txt, Qt::MatchFixedString);
	if(intv != -1) ui->filerate_PD->setCurrentIndex(intv);

	txt = settings.value("bit-depth").toString();
	intv = ui->bitDepthComboBox->findText(txt, Qt::MatchFixedString);
	if(intv != -1) ui->bitDepthComboBox->setCurrentIndex(intv);

	txt = settings.value("time-code-advance").toString();
	intv = ui->advance_CB->findText(txt, Qt::MatchFixedString);
	if(intv != -1) ui->advance_CB->setCurrentIndex(intv);

	txt = settings.value("sidecar").toString();
	intv = ui->xmlSidecarComboBox->findText(txt, Qt::MatchFixedString);
	if(intv != -1) ui->xmlSidecarComboBox->setCurrentIndex(intv);

	ui->audioOutputFilenameEdit->setText(
			settings.value("output-audio-file", "").toString());
	ui->outputVideoCheckBox->setChecked(
			settings.value("output-video-enabled", false).toBool());
	ui->videoOutputFilenameEdit->setText(
			settings.value("output-video-file", "").toString());
	ui->outputFilenameTemplateEdit->setText(
			settings.value("output-name-template", "{source}_{datetime}").toString());
	outputConflictMode = ClampRangeInt(
			settings.value("output-conflict-mode", 0).toInt(), 0, 1);
	ui->outputConflictModeComboBox->setCurrentIndex(outputConflictMode);
	outputOpenFolderAfterExport = settings.value("output-open-folder", false).toBool();
	ui->outputOpenFolderCheckBox->setChecked(outputOpenFolderAfterExport);
	autoCleanupTempFilesEnabled =
			settings.value("output-auto-cleanup-temp", true).toBool();
	ui->outputAutoCleanupTempCheckBox->setChecked(autoCleanupTempFilesEnabled);
	exportReportMode = ClampRangeInt(
			settings.value("output-export-report-mode", EXPORT_REPORT_OFF).toInt(),
			EXPORT_REPORT_OFF, EXPORT_REPORT_BOTH);
	ui->outputExportReportModeComboBox->setCurrentIndex(exportReportMode);
	queueBatchReportMode = ClampRangeInt(
			settings.value("queue-batch-report-mode", QUEUE_BATCH_REPORT_OFF).toInt(),
			QUEUE_BATCH_REPORT_OFF, QUEUE_BATCH_REPORT_BOTH);
	ui->queueBatchReportModeComboBox->setCurrentIndex(queueBatchReportMode);
	ui->videoAvOffsetCheckBox->setChecked(
			settings.value("video-av-offset", true).toBool());
	ui->outputFlipHorizontalCheckBox->setChecked(
			settings.value("video-flip-h", false).toBool());
	ui->outputFlipVerticalCheckBox->setChecked(
			settings.value("video-flip-v", false).toBool());
	ui->outputRotationComboBox->setCurrentIndex(ClampRangeInt(
			settings.value("video-rotation-index", 0).toInt(), 0, 3));

	const bool hasAudioOnlyPreset = settings.contains("audio-only-quality-preset");
	const bool hasAudioPreset = settings.contains("audio-quality-preset");

	audioOnlyQualityPresetSyncLock = true;
	audioQualityPresetSyncLock = true;

	if(hasAudioOnlyPreset)
	{
		intv = ClampRangeInt(
				settings.value("audio-only-quality-preset",
						AUDIO_QUALITY_PRESET_CUSTOM).toInt(),
				AUDIO_QUALITY_PRESET_COMPACT,
				AUDIO_QUALITY_PRESET_CUSTOM);
		ui->audioOnlyQualityPresetComboBox->setCurrentIndex(intv);
	}

	if(hasAudioPreset)
	{
		intv = ClampRangeInt(
				settings.value("audio-quality-preset",
						AUDIO_QUALITY_PRESET_CUSTOM).toInt(),
				AUDIO_QUALITY_PRESET_COMPACT,
				AUDIO_QUALITY_PRESET_CUSTOM);
		ui->audioQualityPresetComboBox->setCurrentIndex(intv);
	}

	audioQualityPresetSyncLock = false;
	audioOnlyQualityPresetSyncLock = false;

	if(!hasAudioOnlyPreset)
		UpdateAudioOnlyQualityPresetFromControls();
	if(!hasAudioPreset)
		UpdateAudioQualityPresetFromControls();

	settings.endGroup();
	UpdateExtractOutputControls();
	ApplyVideoContainerExtensionToOutputFilename();

}

void MainWindow::LicenseAgreement()
{
	QFile licFile(":/LICENSE.txt");

	if(!licFile.open(QIODevice::ReadOnly))
	{
		exit(1);
	}
	QTextStream licStream(&licFile);
    QString lic = licStream.readAll();
	licFile.close();

    QDialog msg(this);
    QLabel *label;
    msg.setWindowTitle("License Agreement");
    msg.setStyleSheet(
            "QDialog { background-color: #d0d0d0; color: #1f1f1f; }"
            "QTextEdit { background-color: #ececec; color: #1f1f1f; "
                    "border: 1px solid #8a8a8a; selection-background-color: #b9d6ff; }"
            "QLabel { background: transparent; color: #1f1f1f; }"
            "QPushButton { background-color: #e0e0e0; color: #1f1f1f; "
                    "border: 1px solid #8a8a8a; padding: 4px 10px; }"
            "QPushButton:hover { background-color: #ebebeb; }"
            "QDialogButtonBox { background: transparent; }");
    QVBoxLayout vbox(&msg);
    label = new QLabel("<b>License Agreement</b>");
    vbox.addWidget(label,0,Qt::AlignCenter);
    label = new QLabel("Copyright (c) 2024 South Carolina Research Foundation");
    vbox.addWidget(label,0,Qt::AlignLeft);
    label = new QLabel("All Rights Reserved");
    vbox.addWidget(label,0,Qt::AlignLeft);

    QTextEdit *text = new QTextEdit();
    text->setText(lic);
    text->setReadOnly(true);
    QFontMetrics fm = text->fontMetrics();
    int w = fm.averageCharWidth();
    text->setMinimumWidth(w*80);
    vbox.addWidget(text,1);

    QDialogButtonBox *buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *acceptButton = new QPushButton("Accept License");
    buttonBox->addButton(acceptButton, QDialogButtonBox::AcceptRole);
    QObject::connect(buttonBox, SIGNAL(accepted()), &msg, SLOT(accept()));
    QObject::connect(buttonBox, SIGNAL(rejected()), &msg, SLOT(reject()));

    vbox.addWidget(buttonBox);

    int ret = msg.exec();

    if(ret != QMessageBox::Accepted) exit(0);

	QSettings settings;
	settings.setValue("license",APP_VERSION);

	return;
}

void MainWindow::Render_Frame()
{
	if (frame_window!=NULL)
	{
		frame_window->renderNow();
	}
}

ExtractedSound MainWindow::ExtractionParamsFromGUI()
{
	ExtractedSound params;

	params.useBounds = ui->OverlapSoundtrackCheckBox->isChecked();
	params.usePixBounds = ui->OverlapPixCheckBox->isChecked();
	if(!params.usePixBounds) params.useBounds = true;

	if(params.useBounds)
	{
		params.bounds[0] = ui->leftSpinBox->value();
		params.bounds[1] = ui->rightSpinBox->value();
	}
	if(params.usePixBounds)
	{
		params.pixBounds[0] = ui->leftPixSpinBox->value();
		params.pixBounds[1] = ui->rightPixSpinBox->value();
	}

	params.framePitch[0] = ui->framepitchstartSlider->value();
	params.framePitch[1] = ui->FramePitchendSlider->value();
	params.videoFrameCrop[0] = ui->videoFrameTopSlider->value();
	params.videoFrameCrop[1] = ui->videoFrameBottomSlider->value();
	params.videoStableCrop = ui->videoStableCropCheckBox->isChecked();
	params.videoDriftSensitivity = ui->videoDriftSensitivitySlider->value();

	params.frameIn = ui->frameInSpinBox->value();
	params.frameOut = ui->frameOutSpinBox->value();

	params.gamma = ui->gammaSlider->value();
	params.gain = ui->gainSlider->value();

	params.useSCurve = ui->threshBox->isChecked();
	if(params.useSCurve) params.sCurve = ui->thresholdSlider->value();

	params.overlap = ui->overlapSlider->value();
	params.lift = ui->liftSlider->value();
	params.blur = ui->blurSlider->value();

	switch(ui->filmrate_PD->currentIndex())
	{
	case 0: params.fpsType = FPS_NTSC; break;
	case 1: params.fpsType = FPS_24; break;
	case 2: params.fpsType = FPS_25; break;
	}

	params.makeNegative = ui->negBox->isChecked();
	params.makeGray = ui->desatBox->isChecked();

    params.rot_angle=ui->degreeSpinBox->value();
    params.applyRotation=ui->rotateCheckbox->isChecked();
	return params;
}

//-----------------------------------------------------------------------------
// ExtractionParametersToGUI
// Copy values from the ExtractedSound params to the GUI controls
void MainWindow::ExtractionParametersToGUI(const ExtractedSound &params)
{
	ui->OverlapSoundtrackCheckBox->setChecked(params.useBounds);
	ui->OverlapPixCheckBox->setChecked(params.usePixBounds);
	on_OverlapPixCheckBox_clicked(ui->OverlapPixCheckBox->isChecked());

	if(params.useBounds)
	{
		ui->leftSpinBox->setValue(params.bounds[0]);
		ui->leftSlider->setValue(params.bounds[0]);
		ui->rightSpinBox->setValue(params.bounds[1]);
		ui->rightSlider->setValue(params.bounds[1]);
	}

	if(params.usePixBounds)
	{
		ui->leftPixSpinBox->setValue(params.pixBounds[0]);
		ui->leftPixSlider->setValue(params.pixBounds[0]);
		ui->rightPixSpinBox->setValue(params.pixBounds[1]);
		ui->rightPixSlider->setValue(params.pixBounds[1]);
	}

	ui->FramePitchendSlider->setValue(params.framePitch[1]);
	ui->framePitchLabel->setText(
				QString::number(params.framePitch[1]/1e3,'f',3));
	ui->framepitchstartSlider->setValue(params.framePitch[0]);
	ui->framepitchstartLabel->setText(
				QString::number(params.framePitch[0]/1e3,'f',3));
	ui->videoFrameTopSlider->setValue(params.videoFrameCrop[0]);
	ui->videoFrameBottomSlider->setValue(params.videoFrameCrop[1]);
	NormalizeVideoCropControls(true);
	UpdateVideoCropLabels();
	ui->videoStableCropCheckBox->setChecked(params.videoStableCrop);
	ui->videoDriftSensitivitySlider->setValue(params.videoDriftSensitivity);
	UpdateVideoStabilizationControls();

	ui->maxFrequencyLabel->setText(
				QString("%1").arg(float(this->MaxFrequency())/1000.0));

	ui->frameInSpinBox->setValue(params.frameIn);
	ui->frameOutSpinBox->setValue(params.frameOut);

	ui->gammaSlider->setValue(params.gamma);
	ui->gainSlider->setValue(params.gain);

	ui->threshBox->setChecked(params.useSCurve);
	if(params.useSCurve)
		ui->thresholdSlider->setValue(params.sCurve);

	ui->overlapSlider->setValue(params.overlap);
	ui->liftSlider->setValue(params.lift);
	ui->blurSlider->setValue(params.blur);

	switch(params.fpsType)
	{
	case FPS_NTSC: ui->filmrate_PD->setCurrentIndex(0); break;
	case FPS_24: ui->filmrate_PD->setCurrentIndex(1); break;
	case FPS_25: ui->filmrate_PD->setCurrentIndex(2); break;
	}

	ui->negBox->setChecked(params.makeNegative);
	ui->desatBox->setChecked(params.makeGray);

    ui->degreeSpinBox->setValue(params.rot_angle);
     ui->rotateCheckbox->setChecked(params.applyRotation);
	GPU_Params_Update(1);
}

//-----------------------------------------------------------------------------
// GUI_Params_Update
// Copy values from the GPU settings in frame_window to the GUI controls
void MainWindow::GUI_Params_Update()
{
	// prevent a loop-back from GPU_Params_Update
	if(paramCopyLock) return;
	paramCopyLock = true;

	// If the OpenGL window isn't ready with a scan, skip the update
	if (frame_window!=NULL && this->scan.inFile.IsReady())
	{
		ui->leftSpinBox->setValue(
				frame_window->bounds[0] * this->scan.inFile.Width());
		ui->leftSlider->setValue(
				frame_window->bounds[0] * this->scan.inFile.Width());
		ui->rightSpinBox->setValue(
				frame_window->bounds[1] * this->scan.inFile.Width());
		ui->rightSlider->setValue(
				frame_window->bounds[1] * this->scan.inFile.Width());

		ui->leftPixSpinBox->setValue(
				frame_window->pixbounds[0] * this->scan.inFile.Width());
		ui->leftPixSlider->setValue(
				frame_window->pixbounds[0] * this->scan.inFile.Width());
		ui->rightPixSpinBox->setValue(
				frame_window->pixbounds[1] * this->scan.inFile.Width());
		ui->rightPixSlider->setValue(
				frame_window->pixbounds[1] * this->scan.inFile.Width());

		ui->FramePitchendSlider->setValue(
				frame_window->overlap[2] * 1000.0f);
		ui->framePitchLabel->setText(
				QString::number(frame_window->overlap[2],'f',3));
		ui->framepitchstartSlider->setValue(frame_window->overlap[3]*1000.0f);
		ui->framepitchstartLabel->setText(
				QString::number(frame_window->overlap[3],'f',3));
		ui->maxFrequencyLabel->setText(
				QString("%1").arg(float(this->MaxFrequency())/1000.0));
	}

	// release the lock
	paramCopyLock = false;
}

//-----------------------------------------------------------------------------
// GPU_Params_Update
// Copy values from the GUI controls to the GPU settings in frame_window
void MainWindow::GPU_Params_Update(bool renderyes)
{
	// prevent a loop-back from GUI_Params_Update
	if(paramCopyLock) return;
	paramCopyLock = true;

	// If the OpenGL window isn't ready with a scan, skip the update
	if (frame_window!=NULL && this->scan.inFile.IsReady())
	{
		frame_window->bounds[0] =
				ui->leftSpinBox->value()/float(this->scan.inFile.Width());
		frame_window->bounds[1] =
				ui->rightSpinBox->value()/float(this->scan.inFile.Width());

		frame_window->pixbounds[0] =
				ui->leftPixSpinBox->value()/float(this->scan.inFile.Width());
		frame_window->pixbounds[1] =
				ui->rightPixSpinBox->value()/float(this->scan.inFile.Width());

		frame_window->overlap[0] = TRANSSLIDER_VALUE/10000.0f;
		frame_window->overlap[1] = ui->overlapSlider->value()/100.0f;
		frame_window->overlap[2] = ui->FramePitchendSlider->value()/1000.0f;
		frame_window->overlap[3] = ui->framepitchstartSlider->value()/1000.0f;
		frame_window->video_crop[0] =
				ui->videoFrameTopSlider->value()/1000.0f;
		frame_window->video_crop[1] =
				ui->videoFrameBottomSlider->value()/1000.0f;
		ui->maxFrequencyLabel->setText(
				QString("%1").arg(float(this->MaxFrequency())/1000.0));

		frame_window->gamma = ui->gammaSlider->value()/100.0f;
		frame_window->lift = ui->liftSlider->value()/100.0f;
		frame_window->gain = ui->gainSlider->value()/100.0f;
		frame_window->blur = ui->blurSlider->value()/100.0f;
		frame_window->threshold = ui->thresholdSlider->value()/100.0f;
		frame_window->thresh = ui->threshBox->checkState();
		frame_window->trackonly = ui->actionShow_Soundtrack_Only->isChecked();
		frame_window->negative = ui->negBox->checkState();
		frame_window->desaturate = ui->desatBox->checkState();
		frame_window->overlapshow = ui->actionShow_Overlap->isChecked();

        if(ui->rotateCheckbox->isChecked())
            frame_window->rot_angle = ui->degreeSpinBox->value();
        else
            frame_window->rot_angle = 0.0f;

		// Note: if none are checked, we still use the soundtrack (target=1)
		if(ui->OverlapPixCheckBox->isChecked())
		{
			if(ui->OverlapSoundtrackCheckBox->isChecked())
				frame_window->overlap_target = 2.0;
			else
				frame_window->overlap_target = 1.0;
		}
		else
			frame_window->overlap_target = 0.0;

		if (ui->actionWaveform_Zoom->isChecked())
			frame_window->WFMzoom=10.0f;
		else
			frame_window->WFMzoom=1.0f;

		if (renderyes)
			frame_window->renderNow();
	}

	// release the lock
	paramCopyLock = false;
}

//-----------------------------------------------------------------------------
bool MainWindow::Load_Frame_Texture(int frame_num)
{
	if (frame_window==NULL) return false;

	if(!frame_window->isExposed())
	{
		Log() << "Frame window not exposed. Attempting to raise.\n";

		frame_window->raise();
		frame_window->showNormal();
		frame_window->setWindowState(Qt::WindowActive);
		//qApp->setActiveWindow(frame_window);

		qApp->processEvents();

		while (!frame_window->isExposed())
		{
			Log() << "Frame window still not exposed. Asking for user help.\n";
			int raise = QMessageBox::question(this, "Select image Window",
					"The image window is obscured. Please move it to the front.",
					QMessageBox::Ok | QMessageBox::Cancel,
					QMessageBox::Ok);

			if(raise == QMessageBox::Cancel)
			{
				Log() << "User cancelled (could not raise window?)\n";
				LogClose();
				return false;
			}
		}
	}

	traceCurrentOperation = "Retrieving scan image";
	currentFrameTexture = this->scan.inFile.GetFrameImage(
			this->scan.inFile.FirstFrame()+frame_num, currentFrameTexture);
	traceCurrentOperation = "Loading scan into texture";
	frame_window->load_frame_texture(currentFrameTexture);

	/*
	traceCurrentOperation = "Freeing texture buffer";
	if (frameTex)
		delete frameTex;
	*/

	traceCurrentOperation = "GL render";
	frame_window->renderNow();
	traceCurrentOperation = "";

	// record frame in static variable for debugging/restarting from error:
	lastFrameLoad = frame_num;
	if(!frame_window->is_rendering && !suppressFrameAutoOptimizeDuringExtraction)
		ApplyAutoOptimizationToFrameControls(false);
	else
		UpdateAutoOptimizationControls();

	return true;
}

int MainWindow::MaxFrequency() const
{
	if(!this->frame_window) return -1;
	if(!this->scan.inFile.IsReady()) return -1;

	float fps;
	switch(this->ui->filmrate_PD->currentIndex())
	{
	case 0: fps = 23.976; break;
	case 1: fps = 24.000; break;
	case 2: fps = 25.000; break;
	}

	int nPitchLines = this->scan.inFile.Height() *
			(1 - frame_window->overlap[3] - frame_window->overlap[2]);

	return int(nPitchLines * fps / 2);
}

void MainWindow::on_sourceButton_clicked()
{
	// Ask for input source

	static QString prevDir = "";
	QString srcDir;

	const struct {
		SourceFormat fileType;
		const char *filter;
	} fileFilterArr[] = {
        { SOURCE_LIBAV,     "Video files (*.mp4 *.mov *.avi)" },
        { SOURCE_OTHER_IMG, "Image frames (*.dpx *.tif *.tiff *.jpg *.jpeg *.JPG *.JPEG *.png *.PNG)" },
        { SOURCE_DPX,       "DPX frames (*.dpx)" },
        { SOURCE_TIFF,      "TIFF frames (*.tif *.tiff)" },
        { SOURCE_JPEG,      "JPEG frames (*.jpg *.jpeg *.JPG *.JPEG)" },
        { SOURCE_PNG,       "PNG frames (*.png *.PNG)" },
        { SOURCE_WAV,       "Synthetic (*.wav)" },
        { SOURCE_UNKNOWN,   "" }
	};

	int ft;

	QStringList fileTypeFilters;
	for(ft = 0; fileFilterArr[ft].fileType != SOURCE_UNKNOWN; ++ft)
		fileTypeFilters += tr(fileFilterArr[ft].filter);

	if(prevDir.isEmpty())
	{
		QSettings settings;
		settings.beginGroup("default-folder");
		srcDir = settings.value("source").toString();
		if(srcDir.isEmpty())
		{
			QStringList l;
			l = QStandardPaths::standardLocations(
						QStandardPaths::DocumentsLocation);
			if(l.size()) srcDir = l.at(0);
			else srcDir = "/";
		}
		settings.endGroup();
	}
	else
	{
		srcDir = prevDir;
	}

	QString selectedType = "";
	QString filename = QFileDialog::getOpenFileName(this,
			tr("Film Source"), srcDir,
			fileTypeFilters.join(";;"),
			&selectedType /* , QFileDialog::DontUseNativeDialog */ );

	if(filename.isEmpty()) return;

	prevDir = QFileInfo(filename).absolutePath();

	ft = fileTypeFilters.indexOf(selectedType);
	if(ft < 0) return;

	this->NewSource(filename, fileFilterArr[ft].fileType);
}

bool MainWindow::NewSource(QString filename, SourceFormat ft)
{
	//Set up longjmp to catch SIGSEGV and report what was going on at the
	// time of the segment violation.
	traceCurrentOperation = "";
	traceSubroutineOperation = "";
	int jmp = setjmp(segvJumpEnv);
	if(jmp!=0)
	{
		QMessageBox::critical(NULL,"Critical Failure",
				QString("Critical failure opening new source.\n%1\n%2").
						arg(traceCurrentOperation).
						arg(traceSubroutineOperation));

		std::exit(1);
	}

	void (*prevSegvHandler)(int);
	prevSegvHandler = std::signal(SIGSEGV, SegvHandler);

	try
	{
		traceCurrentOperation = "Opening Source";
		this->scan.SourceScan(filename.toStdString(), ft);
		traceCurrentOperation = "Verifying scan is ready";
		if(this->scan.inFile.IsReady())
		{
			if(frame_window)
			{
				traceCurrentOperation="Closing previous frame window";
				frame_window->currentOperation = &traceSubroutineOperation;
				frame_window->close();
				traceCurrentOperation="Deleting previous frame window";
				delete frame_window;
			}

			traceCurrentOperation = "Creating new frame window";
			frame_window = new Frame_Window(
						this->scan.inFile.Width(),this->scan.inFile.Height());

			frame_window->currentOperation = &traceSubroutineOperation;
			frame_window->setTitle(filename);
			frame_window->ParamUpdateCallback(&(this->GUI_Params_Update_Static),this);

			Log() << "New frame window\n";
			frame_window->logger = &Log();

			traceCurrentOperation = "Resizing window to 640x640";
			frame_window->resize(640, 640);

			traceCurrentOperation = "Showing frame window";
			frame_window->show();
			PositionPreviewWindowBesideMainWindow();
			qApp->processEvents();

			traceCurrentOperation = "Updating GUI controls for new source";
			ui->frameInSpinBox->setValue(this->scan.inFile.FirstFrame());
			ui->frameInTimeCodeLabel->setText(Compute_Timecode_String(0));
			ui->frameOutSpinBox->setValue(this->scan.inFile.LastFrame());
			ui->frameOutTimeCodeLabel->setText(
						Compute_Timecode_String(this->scan.inFile.NumFrames()-1));
			ui->frameNumberTimeCodeLabel->setText(Compute_Timecode_String(0));

			ui->rightSpinBox->setMaximum(this->scan.inFile.Width()-1);
			ui->leftSpinBox->setMaximum(this->scan.inFile.Width()-1);
			ui->rightSlider->setMaximum(this->scan.inFile.Width()-1);
			ui->leftSlider->setMaximum(this->scan.inFile.Width()-1);

			ui->rightPixSpinBox->setMaximum(this->scan.inFile.Width()-1);
			ui->leftPixSpinBox->setMaximum(this->scan.inFile.Width()-1);
			ui->rightPixSlider->setMaximum(this->scan.inFile.Width()-1);
			ui->leftPixSlider->setMaximum(this->scan.inFile.Width()-1);

			ui->leftPixSlider->setValue(int(this->scan.inFile.Width() * 0.475));
			ui->leftPixSpinBox->setValue(int(this->scan.inFile.Width() * 0.475));
			ui->rightPixSlider->setValue(int(this->scan.inFile.Width() * 0.525));
			ui->rightPixSpinBox->setValue(int(this->scan.inFile.Width() * 0.525));

			ui->playSlider->setMaximum(this->scan.inFile.NumFrames()-1);

			// finalize UI with user preferences
			LoadDefaults();

			// enable the rest of the UI that was waiting until a project loaded
			ui->actionSave_Settings->setEnabled(true);
			ui->actionShow_Overlap->setEnabled(true);
			ui->actionShow_Soundtrack_Only->setEnabled(true);
			ui->actionWaveform_Zoom->setEnabled(true);
			ui->menuView->setEnabled(true);
			ui->saveprojectButton->setEnabled(true);
			ui->loadSettingsButton->setEnabled(true);
			ui->loadLastSettingsButton->setEnabled(true);
			ui->tabWidget->setEnabled(true);
			RecursivelyEnable(ui->viewOptionsLayout, true);
			RecursivelyEnable(ui->frameNumberLayout, true);
			//RecursivelyEnable(ui->sampleLayout, false);
			ui->playSampleButton->setEnabled(true);
			ui->autoLoadSettingsCheckBox->setEnabled(true);

			traceCurrentOperation = "Updating GPU params";
			GPU_Params_Update(0);
			traceCurrentOperation = "Displaying first frame";

			Load_Frame_Texture(0);
		}

	}
	catch(std::exception &e)
	{
		QMessageBox w;
		QString msg("Error opening source: \n");
		int answer;

		msg += QString(e.what());

		w.setText(msg);
		w.setStandardButtons(QMessageBox::Abort | QMessageBox::Ok);
		w.setDefaultButton(QMessageBox::Ok);
		answer = w.exec();

		if(answer == QMessageBox::Abort) exit(1);
		return false;
	}

	traceCurrentOperation = "";
	traceSubroutineOperation = "";
	this->frame_window->logger = NULL;
	this->frame_window->currentOperation = NULL;

	// restore the previous SEGV handler
	if(prevSegvHandler != SIG_ERR)
		std::signal(SIGSEGV, prevSegvHandler);

	return true;
}

//********************* Project Load and Save *********************************
bool MainWindow::saveproject(QString fn)
{
	QFile file(fn);
	if(!file.open(QIODevice::WriteOnly))
	{
		(void)QMessageBox::warning(this, tr("AEO-Light"),
				tr("Could not write project file:\n%1").arg(fn));
		return false;
	}
	QTextStream out(&file);   // we will serialize the data into the file

	saveproject(out);

	file.close();

	return true;
}

bool MainWindow::saveproject(QTextStream &out)
{
	out<<"AEO-Light Project Settings\n";

	// Source Data
	out<<"Source Scan = "<<this->scan.inFile.GetFileName().c_str()<<"\n";
	out<<"Source Format = "<<this->scan.inFile.GetFormatStr()<<"\n";
	out<<"Frame Rate = "<<ui->filmrate_PD->currentText()<<"\n";

	// Sondtrack Settings
	out<<"Use Soundtrack = "<<ui->OverlapSoundtrackCheckBox->isChecked()<<"\n";
	out<<"Left Boundary = "<<ui->leftSpinBox->value()<<"\n";
	out<<"Right Boundary = "<<ui->rightSpinBox->value()<<"\n";
	out<<"Soundtrack Type = "<<ui->monostereo_PD->currentText()<<"\n";
	out<<"Use Pix Track = "<<ui->OverlapPixCheckBox->isChecked()<<"\n";
	out<<"Left Pix Boundary = "<<ui->leftPixSpinBox->value()<<"\n";
	out<<"Right Pix Boundary = "<<ui->rightPixSpinBox->value()<<"\n";
	out<<"Frame Pitch Start = "<<ui->framepitchstartSlider->value()<<"\n";
	out<<"Frame Pitch End = "<<ui->FramePitchendSlider->value()<<"\n";
	out<<"Video Frame Top = "<<ui->videoFrameTopSlider->value()<<"\n";
	out<<"Video Frame Bottom = "<<ui->videoFrameBottomSlider->value()<<"\n";
	out<<"Video Stable Crop = "<<ui->videoStableCropCheckBox->isChecked()<<"\n";
	out<<"Video Drift Sensitivity = "<<ui->videoDriftSensitivitySlider->value()<<"\n";
	out<<"Overlap Search Size = "<<ui->overlapSlider->value()<<"\n";
	out<<"Frame Translation = "<< TRANSSLIDER_VALUE <<"\n";

	// Image Processing Settings
	out<<"Lift = "<<ui->liftSlider->value()<<"\n";
	out<<"Gamma = "<<ui->gammaSlider->value()<<"\n";
	out<<"Gain = "<<ui->gainSlider->value()<<"\n";
	out<<"S-Curve Value = "<<ui->thresholdSlider->value()<<"\n";
	out<<"S-Curve On = "<<ui->threshBox->checkState()<<"\n";
	out<<"Blur = "<<ui->blurSlider->value()<<"\n";
	out<<"Negative = "<<ui->negBox->checkState()<<"\n";
	out<<"Desaturate = "<<ui->desatBox->checkState()<<"\n";
	out<<"Calibrate = "<<ui->CalEnableCB->checkState()<<"\n";

	#ifdef SAVE_CALIBRATION_MASK_IN_PROJECT
	int numFloats = frame_window->cal_points;
	float *mask = frame_window->GetCalibrationMask();
	uchar *cmask = reinterpret_cast<uchar *>(mask);
	QByteArray bytes = qCompress(cmask, numFloats*sizeof(float), 9);
	out<<"Calibration Mask = "<<(bytes.toBase64())<<"\n";

	cmask=NULL;
	delete [] mask;
	mask = NULL;
	#endif

	// Extraction Settings
	out<<"Export Frame In = "<<ui->frameInSpinBox->value()<<"\n";
	out<<"Export Frame Out = "<<ui->frameOutSpinBox->value()<<"\n";
	out<<"Export Sampling Rate = "<<ui->filerate_PD->currentText()<<"\n";
	out<<"Export Bit Depth = "<<ui->bitDepthComboBox->currentText()<<"\n";
	// This is probably more of a source setting than an export setting:
	out<<"Timecode Advance = "<<ui->advance_CB->currentIndex()<<"\n";
	out<<"Export XML Sidecar = "<<ui->xmlSidecarComboBox->currentText()<<"\n";
	out<<"Output Filename Template = "
			<<ui->outputFilenameTemplateEdit->text()<<"\n";
	out<<"Output Conflict Mode = "
			<<ui->outputConflictModeComboBox->currentIndex()<<"\n";
	out<<"Output Open Folder = "
			<<ui->outputOpenFolderCheckBox->isChecked()<<"\n";
	out<<"Output Auto Cleanup Temp = "
			<<ui->outputAutoCleanupTempCheckBox->isChecked()<<"\n";
	out<<"Output Export Report Mode = "
			<<ui->outputExportReportModeComboBox->currentIndex()<<"\n";
	out<<"Queue Batch Report Mode = "
			<<ui->queueBatchReportModeComboBox->currentIndex()<<"\n";

	// View Settings
	out<<"View Zoom = "<<ui->waveformZoomCheckBox->isChecked()<<"\n";
	out<<"View Overlap = "<<ui->showOverlapCheckBox->isChecked()<<"\n";
	out<<"View Soundtrack Only = "<<
			ui->showSoundtrackOnlyCheckBox->isChecked()<<"\n";
	out<<"View Frame = "<<ui->frame_numberSpinBox->value()<<"\n";

	return true;
}

QString MainWindow::SerializeCurrentSettings()
{
	QString serialized;
	QTextStream stream(&serialized);
	saveproject(stream);
	return serialized;
}

bool MainWindow::LoadSettingsFromSerializedText(const QString &serialized)
{
	if(serialized.trimmed().isEmpty())
		return false;

	QTemporaryFile temp;
	temp.setAutoRemove(true);
	if(!temp.open())
		return false;

	QTextStream out(&temp);
	out << serialized;
	out.flush();
	temp.flush();

	return LoadProjectSettings(temp.fileName());
}

void MainWindow::SaveLastUsedSettingsSnapshot()
{
	QSettings settings;
	settings.beginGroup("ui");
	settings.setValue("last-used-settings", SerializeCurrentSettings());
	settings.setValue("last-used-settings-ts", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	settings.endGroup();
}

bool MainWindow::LoadLastUsedSettingsSnapshot()
{
	if(!this->scan.inFile.IsReady())
	{
		QMessageBox::information(
				this,
				tr("Load Last Settings"),
				tr("Open eerst een project/bron voordat je instellingen laadt."));
		return false;
	}

	QSettings settings;
	settings.beginGroup("ui");
	const QString serialized = settings.value("last-used-settings", "").toString();
	settings.endGroup();
	if(serialized.trimmed().isEmpty())
	{
		QMessageBox::information(
				this,
				tr("Load Last Settings"),
				tr("Er zijn nog geen laatst gebruikte instellingen opgeslagen."));
		return false;
	}
	return LoadSettingsFromSerializedText(serialized);
}

QStringList MainWindow::SavedPresetNames() const
{
	QSettings settings;
	settings.beginGroup("settings-presets");
	QStringList names = settings.childKeys();
	settings.endGroup();
	names.sort(Qt::CaseInsensitive);
	return names;
}

void MainWindow::SaveCurrentSettingsPreset()
{
	if(!this->scan.inFile.IsReady())
	{
		QMessageBox::information(
				this,
				tr("Save Settings Preset"),
				tr("Open eerst een project/bron voordat je een preset bewaart."));
		return;
	}

	QString presetName = QInputDialog::getText(
			this,
			tr("Save Settings Preset"),
			tr("Presetnaam:"));
	presetName = presetName.trimmed();
	if(presetName.isEmpty())
		return;

	QSettings settings;
	settings.beginGroup("settings-presets");
	const bool exists = settings.contains(presetName);
	settings.endGroup();
	if(exists)
	{
		const int answer = QMessageBox::question(
				this,
				tr("Preset overschrijven"),
				tr("Preset '%1' bestaat al. Overschrijven?").arg(presetName),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
		if(answer != QMessageBox::Yes)
			return;
	}

	settings.beginGroup("settings-presets");
	settings.setValue(presetName, SerializeCurrentSettings());
	settings.endGroup();
}

void MainWindow::LoadSettingsPreset()
{
	if(!this->scan.inFile.IsReady())
	{
		QMessageBox::information(
				this,
				tr("Load Settings Preset"),
				tr("Open eerst een project/bron voordat je een preset laadt."));
		return;
	}

	const QStringList names = SavedPresetNames();
	if(names.isEmpty())
	{
		QMessageBox::information(
				this,
				tr("Load Settings Preset"),
				tr("Er zijn nog geen opgeslagen presets."));
		return;
	}

	bool ok = false;
	const QString selected = QInputDialog::getItem(
			this,
			tr("Load Settings Preset"),
			tr("Kies preset:"),
			names,
			0,
			false,
			&ok);
	if(!ok || selected.isEmpty())
		return;

	QSettings settings;
	settings.beginGroup("settings-presets");
	const QString serialized = settings.value(selected, "").toString();
	settings.endGroup();
	LoadSettingsFromSerializedText(serialized);
}

void MainWindow::DeleteSettingsPreset()
{
	const QStringList names = SavedPresetNames();
	if(names.isEmpty())
	{
		QMessageBox::information(
				this,
				tr("Delete Settings Preset"),
				tr("Er zijn geen presets om te verwijderen."));
		return;
	}

	bool ok = false;
	const QString selected = QInputDialog::getItem(
			this,
			tr("Delete Settings Preset"),
			tr("Kies preset:"),
			names,
			0,
			false,
			&ok);
	if(!ok || selected.isEmpty())
		return;

	const int answer = QMessageBox::question(
			this,
			tr("Preset verwijderen"),
			tr("Preset '%1' verwijderen?").arg(selected),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
	if(answer != QMessageBox::Yes)
		return;

	QSettings settings;
	settings.beginGroup("settings-presets");
	settings.remove(selected);
	settings.endGroup();
}

QStringList MainWindow::SavedQueuePresetNames() const
{
	QSettings settings;
	settings.beginGroup("queue-presets");
	QStringList names = settings.childKeys();
	settings.endGroup();
	names.sort(Qt::CaseInsensitive);
	return names;
}

void MainWindow::SaveQueuePreset()
{
	if(this->extractQueue.empty())
	{
		QMessageBox::information(
				this,
				tr("Save Queue Preset"),
				tr("De queue is leeg; er is niets om op te slaan."));
		return;
	}

	QString presetName = QInputDialog::getText(
			this,
			tr("Save Queue Preset"),
			tr("Presetnaam:"));
	presetName = presetName.trimmed();
	if(presetName.isEmpty())
		return;

	QSettings settings;
	settings.beginGroup("queue-presets");
	const bool exists = settings.contains(presetName);
	settings.endGroup();
	if(exists)
	{
		const int answer = QMessageBox::question(
				this,
				tr("Preset overschrijven"),
				tr("Queue preset '%1' bestaat al. Overschrijven?").arg(presetName),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
		if(answer != QMessageBox::Yes)
			return;
	}

	QVariantList serializedTasks;
	for(const ExtractTask &task : this->extractQueue)
		serializedTasks.push_back(SerializeExtractTaskMap(task));

	settings.beginGroup("queue-presets");
	settings.setValue(presetName, serializedTasks);
	settings.endGroup();
}

void MainWindow::LoadQueuePreset()
{
	const QStringList names = SavedQueuePresetNames();
	if(names.isEmpty())
	{
		QMessageBox::information(
				this,
				tr("Load Queue Preset"),
				tr("Er zijn nog geen opgeslagen queue presets."));
		return;
	}

	bool ok = false;
	const QString selected = QInputDialog::getItem(
			this,
			tr("Load Queue Preset"),
			tr("Kies queue preset:"),
			names,
			0,
			false,
			&ok);
	if(!ok || selected.isEmpty())
		return;

	QSettings settings;
	settings.beginGroup("queue-presets");
	const QVariantList serializedTasks = settings.value(selected).toList();
	settings.endGroup();
	if(serializedTasks.isEmpty())
	{
		QMessageBox::warning(
				this,
				tr("Load Queue Preset"),
				tr("De gekozen queue preset bevat geen bruikbare items."));
		return;
	}

	std::vector<ExtractTask> loadedTasks;
	for(const QVariant &entry : serializedTasks)
	{
		ExtractTask task;
		if(DeserializeExtractTaskMap(entry.toMap(), task))
			loadedTasks.push_back(task);
	}
	if(loadedTasks.empty())
	{
		QMessageBox::warning(
				this,
				tr("Load Queue Preset"),
				tr("De gekozen queue preset kon niet worden gelezen."));
		return;
	}

	if(!this->extractQueue.empty())
	{
		const int answer = QMessageBox::question(
				this,
				tr("Queue preset laden"),
				tr("Huidige queue vervangen? (Nee = toevoegen)"),
				QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
				QMessageBox::Yes);
		if(answer == QMessageBox::Cancel)
			return;
		if(answer == QMessageBox::Yes)
			this->extractQueue.clear();
	}

	for(const ExtractTask &task : loadedTasks)
	{
		if(this->extractQueue.size() >= 5)
			break;
		this->extractQueue.push_back(task);
	}

	UpdateQueueWidgets();
}

void MainWindow::DeleteQueuePreset()
{
	const QStringList names = SavedQueuePresetNames();
	if(names.isEmpty())
	{
		QMessageBox::information(
				this,
				tr("Delete Queue Preset"),
				tr("Er zijn geen queue presets om te verwijderen."));
		return;
	}

	bool ok = false;
	const QString selected = QInputDialog::getItem(
			this,
			tr("Delete Queue Preset"),
			tr("Kies queue preset:"),
			names,
			0,
			false,
			&ok);
	if(!ok || selected.isEmpty())
		return;

	const int answer = QMessageBox::question(
			this,
			tr("Preset verwijderen"),
			tr("Queue preset '%1' verwijderen?").arg(selected),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
	if(answer != QMessageBox::Yes)
		return;

	QSettings settings;
	settings.beginGroup("queue-presets");
	settings.remove(selected);
	settings.endGroup();
}

void MainWindow::RetryFailedQueueItems()
{
	if(queueBatchRunning)
	{
		QMessageBox::information(
				this,
				tr("Batch bezig"),
				tr("Wacht tot de huidige batch klaar is voor je Retry uitvoert."));
		return;
	}

	if(failedQueueItems.empty())
	{
		QMessageBox::information(
				this,
				tr("Geen gefaalde items"),
				tr("Er zijn geen gefaalde queue-items om opnieuw te proberen."));
		return;
	}

	std::vector<ExtractTask> merged = failedQueueItems;
	merged.insert(merged.end(), extractQueue.begin(), extractQueue.end());
	const size_t requestedCount = merged.size();
	size_t droppedCount = 0;
	if(merged.size() > 5)
	{
		droppedCount = merged.size() - 5;
		merged.resize(5);
		QMessageBox::information(
				this,
				tr("Queue limiet"),
				tr("Retry toegevoegd: %1 item(s). %2 item(s) niet toegevoegd door queue-limiet.")
						.arg(merged.size())
						.arg(droppedCount));
	}
	extractQueue = merged;
	failedQueueItems.clear();
	UpdateQueueWidgets();
	QString retryStatus = tr("Batch status: retry van gefaalde items gestart (%1/%2 in queue).")
			.arg(extractQueue.size())
			.arg(requestedCount);
	if(droppedCount > 0)
	{
		retryStatus += tr(" %1 item(s) over limiet.").arg(droppedCount);
	}
	ui->queueBatchStatusLabel->setText(retryStatus);
	on_queueExtractButton_clicked();
}

QStringList MainWindow::WriteQueueBatchReport(
		size_t totalCount,
		size_t successCount,
		size_t failedCount,
		size_t skippedCount,
		size_t pendingCount,
		double elapsedSec,
		const QMap<QString, int> &failureReasonCounts,
		const QList<double> &itemDurationsSec,
		const QStringList &skippedItemLines,
		const QStringList &itemLines) const
{
	double minItemSec = 0.0;
	double maxItemSec = 0.0;
	double avgItemSec = 0.0;
	if(!itemDurationsSec.isEmpty())
	{
		double sum = 0.0;
		minItemSec = std::numeric_limits<double>::max();
		maxItemSec = 0.0;
		for(double seconds : itemDurationsSec)
		{
			const double safeSeconds = std::max(0.0, seconds);
			sum += safeSeconds;
			minItemSec = std::min(minItemSec, safeSeconds);
			maxItemSec = std::max(maxItemSec, safeSeconds);
		}
		avgItemSec = sum / std::max(1, itemDurationsSec.size());
	}

	QList<QPair<QString, int>> sortedFailureReasons;
	for(auto it = failureReasonCounts.constBegin();
			it != failureReasonCounts.constEnd(); ++it)
	{
		sortedFailureReasons.append(qMakePair(it.key(), it.value()));
	}
	std::sort(
			sortedFailureReasons.begin(),
			sortedFailureReasons.end(),
			[](const QPair<QString, int> &a, const QPair<QString, int> &b)
	{
		if(a.second != b.second)
			return a.second > b.second;
		return a.first.toLower() < b.first.toLower();
	});

	const int mode = ClampRangeInt(
			queueBatchReportMode,
			QUEUE_BATCH_REPORT_OFF,
			QUEUE_BATCH_REPORT_BOTH);
	if(mode == QUEUE_BATCH_REPORT_OFF)
		return QStringList();

	QString baseDir = prevExportDir;
	if(baseDir.isEmpty())
	{
		baseDir = ReadLastExportDirMain(
				"last-audio-export",
				QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	}
	if(baseDir.isEmpty())
		baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

	const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
	const QString basePath = QDir(baseDir).filePath(QString("aeo-batch-report-%1").arg(stamp));
	QStringList generatedFiles;

	if(mode == QUEUE_BATCH_REPORT_TXT || mode == QUEUE_BATCH_REPORT_BOTH)
	{
		const QString txtPath = basePath + ".txt";
		QFile txtFile(txtPath);
		if(txtFile.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			QTextStream out(&txtFile);
			out << "AEO-Light batch report\n";
			out << "Timestamp = " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
			out << "Total = " << totalCount << "\n";
			out << "Success = " << successCount << "\n";
			out << "Failed = " << failedCount << "\n";
			out << "Skipped = " << skippedCount << "\n";
			out << "Pending = " << pendingCount << "\n";
			out << "Processed = " << itemDurationsSec.size() << "\n";
			out << "Elapsed seconds = " << QString::number(elapsedSec, 'f', 3) << "\n";
			out << "Average seconds per item = " <<
					QString::number(
							elapsedSec / std::max<double>(1.0, double(totalCount)),
							'f',
							3) << "\n";
			if(!itemDurationsSec.isEmpty())
			{
				out << "Min seconds per processed item = "
					<< QString::number(minItemSec, 'f', 3) << "\n";
				out << "Max seconds per processed item = "
					<< QString::number(maxItemSec, 'f', 3) << "\n";
				out << "Avg seconds per processed item = "
					<< QString::number(avgItemSec, 'f', 3) << "\n";
			}
			if(!sortedFailureReasons.isEmpty())
			{
				out << "Failure reason counts:\n";
				for(const QPair<QString, int> &entry : sortedFailureReasons)
				{
					out << " - " << entry.first << " = " << entry.second << "\n";
				}
				out << "Top failure reason = "
					<< sortedFailureReasons.first().first
					<< " ("
					<< sortedFailureReasons.first().second
					<< "x)\n";
			}
			if(!skippedItemLines.isEmpty())
			{
				out << "\nSkipped items:\n";
				for(const QString &line : skippedItemLines)
					out << " - " << line << "\n";
			}
			out << "\nItems:\n";
			for(const QString &line : itemLines)
				out << " - " << line << "\n";
			txtFile.close();
			generatedFiles << txtPath;
		}
	}

	if(mode == QUEUE_BATCH_REPORT_JSON || mode == QUEUE_BATCH_REPORT_BOTH)
	{
		const QString jsonPath = basePath + ".json";
		QFile jsonFile(jsonPath);
		if(jsonFile.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			QJsonObject root;
			root.insert("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
			root.insert("total", static_cast<int>(totalCount));
			root.insert("success", static_cast<int>(successCount));
			root.insert("failed", static_cast<int>(failedCount));
			root.insert("skipped", static_cast<int>(skippedCount));
			root.insert("pending", static_cast<int>(pendingCount));
			root.insert("processed", itemDurationsSec.size());
			root.insert("elapsedSeconds", elapsedSec);
			root.insert(
					"avgSecondsPerItem",
					elapsedSec / std::max<double>(1.0, double(totalCount)));
			if(!itemDurationsSec.isEmpty())
			{
				root.insert("minSecondsPerProcessedItem", minItemSec);
				root.insert("maxSecondsPerProcessedItem", maxItemSec);
				root.insert("avgSecondsPerProcessedItem", avgItemSec);
			}
			QJsonObject reasonCountsObj;
			QJsonArray reasonCountsOrdered;
			for(const QPair<QString, int> &entry : sortedFailureReasons)
			{
				reasonCountsObj.insert(entry.first, entry.second);
				QJsonObject orderedEntry;
				orderedEntry.insert("reason", entry.first);
				orderedEntry.insert("count", entry.second);
				reasonCountsOrdered.append(orderedEntry);
			}
			root.insert("failureReasonCounts", reasonCountsObj);
			root.insert("failureReasonCountsOrdered", reasonCountsOrdered);
			if(!sortedFailureReasons.isEmpty())
			{
				QJsonObject topFailure;
				topFailure.insert("reason", sortedFailureReasons.first().first);
				topFailure.insert("count", sortedFailureReasons.first().second);
				root.insert("topFailureReason", topFailure);
			}
			QJsonArray skippedItems;
			for(const QString &line : skippedItemLines)
				skippedItems.append(line);
			root.insert("skippedItems", skippedItems);
			QJsonArray items;
			for(const QString &line : itemLines)
				items.append(line);
			root.insert("items", items);

			jsonFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
			jsonFile.close();
			generatedFiles << jsonPath;
		}
	}

	if(generatedFiles.isEmpty())
		return generatedFiles;

	Log() << "Batch report saved: "
		  << generatedFiles.join(", ") << "\n";
	return generatedFiles;
}

void MainWindow::WriteSingleExportReport(
		const QString &audioPath,
		const QString &videoPath,
		bool success,
		bool canceled,
		long firstFrame,
		long numFrames,
		double elapsedSec,
		const QString &timingMsg,
		const QString &processMsg) const
{
	const int mode = ClampRangeInt(
			exportReportMode,
			EXPORT_REPORT_OFF,
			EXPORT_REPORT_BOTH);
	if(mode == EXPORT_REPORT_OFF)
		return;

	QString baseDir = QFileInfo(audioPath).absolutePath();
	if(baseDir.isEmpty() && !videoPath.isEmpty())
		baseDir = QFileInfo(videoPath).absolutePath();
	if(baseDir.isEmpty())
		baseDir = prevExportDir;
	if(baseDir.isEmpty())
	{
		baseDir = ReadLastExportDirMain(
				"last-audio-export",
				QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	}
	if(baseDir.isEmpty())
		baseDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

	const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
	const QString basePath = QDir(baseDir).filePath(
			QString("aeo-export-report-%1").arg(stamp));
	QStringList generatedFiles;
	const QString statusText = success ?
			tr("Gelukt") :
			(canceled ? tr("Geannuleerd") : tr("Mislukt"));
	const QString frameOutText = QString::number(firstFrame + std::max<long>(0, numFrames - 1));

	if(mode == EXPORT_REPORT_TXT || mode == EXPORT_REPORT_BOTH)
	{
		const QString txtPath = basePath + ".txt";
		QFile txtFile(txtPath);
		if(txtFile.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			QTextStream out(&txtFile);
			out << "AEO-Light export report\n";
			out << "Timestamp = " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
			out << "Status = " << statusText << "\n";
			out << "Elapsed seconds = " << QString::number(elapsedSec, 'f', 3) << "\n";
			out << "Audio output = " << QDir::toNativeSeparators(audioPath) << "\n";
			out << "Video output = "
				<< (videoPath.isEmpty() ? "-" : QDir::toNativeSeparators(videoPath)) << "\n";
			out << "Frame in/out = " << firstFrame << " / " << frameOutText << "\n";
			out << "Frame count = " << numFrames << "\n";
			out << "Sampling rate = " << ui->filerate_PD->currentText() << "\n";
			out << "Bit depth = " << ui->bitDepthComboBox->currentText() << "\n";
			out << "Export profile = " << ui->videoExportTargetProfileComboBox->currentText() << "\n";
			out << "Video mode = " << ui->videoExportModeComboBox->currentText() << "\n";
			out << "Video size = " << ui->videoExportResolutionComboBox->currentText() << "\n";
			out << "Video container = " << ui->videoExportContainerComboBox->currentText() << "\n";
			out << "Video codec = " << ui->videoExportCodecComboBox->currentText() << "\n";
			out << "Encoder preset = " << ui->videoExportPresetComboBox->currentText() << "\n";
			out << "Video quality (%) = " << ui->videoExportQualitySlider->value() << "\n";
			out << "Audio bitrate (kbps) = " << ui->videoExportAudioBitrateSpinBox->value() << "\n";
			out << "Name template = " << ui->outputFilenameTemplateEdit->text() << "\n";
			out << "Output conflict mode = " << ui->outputConflictModeComboBox->currentText() << "\n";
			out << "Open folder after export = " << ui->outputOpenFolderCheckBox->isChecked() << "\n";
			out << "Auto cleanup temp = " << ui->outputAutoCleanupTempCheckBox->isChecked() << "\n";
			out << "Timing message = " << timingMsg << "\n";
			out << "Process message = " << processMsg << "\n";
			txtFile.close();
			generatedFiles << txtPath;
		}
	}

	if(mode == EXPORT_REPORT_JSON || mode == EXPORT_REPORT_BOTH)
	{
		const QString jsonPath = basePath + ".json";
		QFile jsonFile(jsonPath);
		if(jsonFile.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			QJsonObject root;
			root.insert("timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
			root.insert("status", statusText);
			root.insert("success", success);
			root.insert("canceled", canceled);
			root.insert("elapsedSeconds", elapsedSec);
			root.insert("audioOutput", QDir::toNativeSeparators(audioPath));
			root.insert("videoOutput", videoPath.isEmpty() ? QString("-") :
					QDir::toNativeSeparators(videoPath));
			root.insert("frameIn", static_cast<int>(firstFrame));
			root.insert("frameOut", static_cast<int>(firstFrame + std::max<long>(0, numFrames - 1)));
			root.insert("frameCount", static_cast<int>(numFrames));
			root.insert("samplingRate", ui->filerate_PD->currentText());
			root.insert("bitDepth", ui->bitDepthComboBox->currentText());
			root.insert("exportProfile", ui->videoExportTargetProfileComboBox->currentText());
			root.insert("videoMode", ui->videoExportModeComboBox->currentText());
			root.insert("videoSize", ui->videoExportResolutionComboBox->currentText());
			root.insert("videoContainer", ui->videoExportContainerComboBox->currentText());
			root.insert("videoCodec", ui->videoExportCodecComboBox->currentText());
			root.insert("encoderPreset", ui->videoExportPresetComboBox->currentText());
			root.insert("videoQualityPercent", ui->videoExportQualitySlider->value());
			root.insert("audioBitrateKbps", ui->videoExportAudioBitrateSpinBox->value());
			root.insert("nameTemplate", ui->outputFilenameTemplateEdit->text());
			root.insert("outputConflictMode", ui->outputConflictModeComboBox->currentText());
			root.insert("openFolderAfterExport", ui->outputOpenFolderCheckBox->isChecked());
			root.insert("autoCleanupTemp", ui->outputAutoCleanupTempCheckBox->isChecked());
			root.insert("timingMessage", timingMsg);
			root.insert("processMessage", processMsg);

			jsonFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
			jsonFile.close();
			generatedFiles << jsonPath;
		}
	}

	if(generatedFiles.isEmpty())
		return;

	Log() << "Export report saved: "
		  << generatedFiles.join(", ") << "\n";
}

bool MainWindow::OpenProject(QString fn)
{
	if(LoadProjectSource(fn))
		return LoadProjectSettings(fn);
	return false;
}

void MainWindow::OpenStartingProject()
{
	Log() << "Opening starting project\n";
	if(!this->startingProjectFilename.isEmpty())
		this->OpenProject(this->startingProjectFilename);
}

bool MainWindow::LoadProjectSource(QString fn)
{
	QFile file(fn);
	if(!file.open(QIODevice::ReadOnly))
	{
		(void)QMessageBox::warning(this, tr("AEO-Light"),
				tr("Could not read project file:\n%1").arg(fn));
		return false;
	}
	QTextStream in(&file);   // we will serialize the data into the file
	bool foundSource = false;
	bool foundFormat = false;
	SourceFormat f = SOURCE_UNKNOWN;
	QString srcfn;

	while(!in.atEnd() && !(foundSource && foundFormat)) {
		QString line = in.readLine();
		QStringList fields = line.split(QRegularExpression("\\s*=\\s*"));
		if(fields.size() < 2) continue;
		QString key = fields[0];
		QString value = fields.mid(1).join("=");

		if(key.contains("Source Scan"))
		{
			foundSource = true;
			srcfn = value;
		}
		if(key.contains("Source Format"))
		{
			f = FilmScan::StrToSourceFormat(value.toStdString().c_str());
			foundFormat = true;
		}
	}
	file.close();

	if(!foundSource)
	{
		(void)QMessageBox::warning(this, tr("AEO-Light"),
				tr("This .aeo file doesn't contain the name of a source scan.\n"
				"(It was created by the former [save settings] action)."));
		return false;
	}
	this->NewSource(srcfn, f);

	return true;
}

bool MainWindow::LoadProjectSettings(QString fn)
{
	QFile file(fn);
	if(!file.open(QIODevice::ReadOnly))
	{
		(void)QMessageBox::warning(this, tr("AEO-Light"),
				tr("Could not read project file:\n%1").arg(fn));
		return false;
	}
	QTextStream in(&file);   // we will serialize the data into the file

	bool deleteMe = false;
	bool needMask = false;
	bool gotMask = false;

	while(!in.atEnd()) {
		QString line = in.readLine();
		QStringList fields = line.split(QRegularExpression("\\s*=\\s*"));
		if(fields.size() < 2) continue;
		QString key = fields[0];
		QString value = fields.mid(1).join("=");

		if (key.contains("Frame Rate"))
		{
			int idx = ui->filmrate_PD->findText(value,Qt::MatchExactly);
			if(idx != -1) ui->filmrate_PD->setCurrentIndex(idx);
		}
		if (key.contains("Use Soundtrack"))
			ui->OverlapSoundtrackCheckBox->setChecked(value.toInt());
		if (key.contains("Left Bound"))
			ui->leftSpinBox->setValue(value.toInt());
		if (key.contains("Right Bound"))
			ui->rightSpinBox->setValue(value.toInt());
		if (key.contains("Soundtrack Type"))
		{
			int idx = ui->monostereo_PD->findText(value,Qt::MatchExactly);
			if(idx != -1) ui->monostereo_PD->setCurrentIndex(idx);
		}
		if (key.contains("Use Pix Track"))
			ui->OverlapPixCheckBox->setChecked(value.toInt());
		if (key.contains("Left Pix Bound"))
			ui->leftPixSpinBox->setValue(value.toInt());
		if (key.contains("Right Pix Bound"))
			ui->rightPixSpinBox->setValue(value.toInt());
		if (key.contains("Frame Pitch Start"))
		{
			ui->framepitchstartSlider->setValue(value.toInt());
			ui->framepitchstartLabel->setText(
					QString::number(value.toInt()/1000.f,'f',3));
		}
		if (key.contains("Frame Pitch End"))
			ui->FramePitchendSlider->setValue(value.toInt());
		if (key.contains("Video Frame Top"))
			ui->videoFrameTopSlider->setValue(value.toInt());
		if (key.contains("Video Frame Bottom"))
			ui->videoFrameBottomSlider->setValue(value.toInt());
		if (key.contains("Video Stable Crop"))
			ui->videoStableCropCheckBox->setChecked(value.toInt());
		if (key.contains("Video Drift Sensitivity"))
			ui->videoDriftSensitivitySlider->setValue(value.toInt());
		if (key.contains("Overlap Search Size"))
			ui->overlapSlider->setValue(value.toInt());
		if (key.contains("Lift"))
			ui->liftSlider->setValue(value.toInt());
		if (key.contains("Gamma"))
			ui->gammaSlider->setValue(value.toInt());
		if (key.contains("Gain"))
			ui->gainSlider->setValue(value.toInt());
		if (key.contains("S-Curve Value"))
			ui->thresholdSlider->setValue(value.toInt());
		if (key.contains("S-Curve On"))
			ui->threshBox->setChecked(value.toInt());
		if (key.contains("Blur"))
			ui->blurSlider->setValue(value.toInt());
		if (key.contains("Negative"))
			ui->negBox->setChecked(value.toInt());
		if (key.contains("Desaturate"))
			ui->desatBox->setChecked(value.toInt());

		if (key.contains("Calibrate"))
		{
			ui->CalEnableCB->setChecked(value.toInt());
			needMask = true;
		}

		#ifdef SAVE_CALIBRATION_MASK_IN_PROJECT
		if(key.contains("Calibration Mask"))
		{
			QByteArray bytes = qUncompress(value.toUtf8());
			const char *cmask = bytes.constData();
			const float *mask = reinterpret_cast<const float *>(cmask);
			frame_window->SetCalibrationMask(mask);
			gotMask = true;
		}
		#endif

		// Extraction Settings
		if (key.contains("Export Frame In"))
			ui->frameInSpinBox->setValue(value.toInt());
		if (key.contains("Export Frame Out"))
			ui->frameOutSpinBox->setValue(value.toInt());
		if (key.contains("Export Sampling Rate"))
		{
			int idx = ui->filerate_PD->findText(value,Qt::MatchExactly);
			if(idx != -1) ui->filerate_PD->setCurrentIndex(idx);
		}
		if (key.contains("Export Bit Depth"))
		{
			int idx = ui->bitDepthComboBox->
					findText(value,Qt::MatchExactly);
			if(idx != -1) ui->bitDepthComboBox->setCurrentIndex(idx);
		}
		// This is probably more of a source setting than an export setting:
		if (key.contains("Timecode Advance"))
			ui->advance_CB->setCurrentIndex(value.toInt());

		if (key.contains("Export XML Sidecar"))
		{
			int idx = ui->xmlSidecarComboBox->
					findText(value,Qt::MatchExactly);
			if(idx != -1) ui->xmlSidecarComboBox->setCurrentIndex(idx);
		}
		if (key.contains("Output Filename Template"))
			ui->outputFilenameTemplateEdit->setText(value);
		if (key.contains("Output Conflict Mode"))
			ui->outputConflictModeComboBox->setCurrentIndex(
					ClampRangeInt(value.toInt(), 0, 1));
		if (key.contains("Output Open Folder"))
			ui->outputOpenFolderCheckBox->setChecked(value.toInt());
		if (key.contains("Output Auto Cleanup Temp"))
			ui->outputAutoCleanupTempCheckBox->setChecked(value.toInt());
		if (key.contains("Output Export Report Mode"))
			ui->outputExportReportModeComboBox->setCurrentIndex(
					ClampRangeInt(value.toInt(),
							EXPORT_REPORT_OFF,
							EXPORT_REPORT_BOTH));
		if (key.contains("Queue Batch Report Mode"))
			ui->queueBatchReportModeComboBox->setCurrentIndex(
					ClampRangeInt(value.toInt(),
							QUEUE_BATCH_REPORT_OFF,
							QUEUE_BATCH_REPORT_BOTH));

		// View Settings
		if (key.contains("View Zoom"))
			ui->waveformZoomCheckBox->setChecked(value.toInt());
		if (key.contains("View Overlap"))
			ui->showOverlapCheckBox->setChecked(value.toInt());
		if (key.contains("View Soundtrack Only"))
			ui->showSoundtrackOnlyCheckBox->setChecked(value.toInt());
		if (key.contains("View Frame"))
			ui->frame_numberSpinBox->setValue(value.toInt());

		if(key.contains("DeleteMe"))
			deleteMe = (value.contains("true"));
	}

	file.close();
	if(deleteMe) file.remove();

	if(gotMask)
		ui->CalEnableCB->setEnabled(true);
	else if(needMask)
		QTimer::singleShot(20, this, SLOT(on_CalBtn_clicked()));

	on_OverlapPixCheckBox_clicked(ui->OverlapPixCheckBox->isChecked());
	NormalizeVideoCropControls(true);
	UpdateVideoCropLabels();
	UpdateVideoStabilizationControls();
	outputConflictMode = ClampRangeInt(
			ui->outputConflictModeComboBox->currentIndex(), 0, 1);
	outputOpenFolderAfterExport = ui->outputOpenFolderCheckBox->isChecked();
	autoCleanupTempFilesEnabled = ui->outputAutoCleanupTempCheckBox->isChecked();
	exportReportMode = ClampRangeInt(
			ui->outputExportReportModeComboBox->currentIndex(),
			EXPORT_REPORT_OFF,
			EXPORT_REPORT_BOTH);
	queueBatchReportMode = ClampRangeInt(
			ui->queueBatchReportModeComboBox->currentIndex(),
			QUEUE_BATCH_REPORT_OFF,
			QUEUE_BATCH_REPORT_BOTH);

	return true;
}


void MainWindow::on_saveprojectButton_clicked()
{
	QString savDir;

	if(this->prevProjectDir.isEmpty())
	{
		QSettings settings;
		settings.beginGroup("default-folder");
		savDir = settings.value("project").toString();
		if(savDir.isEmpty())
		{
			savDir = QStandardPaths::writableLocation(
						QStandardPaths::DocumentsLocation);
		}
		settings.endGroup();
	}
	else
	{
		savDir = this->prevProjectDir;
	}

	QString fileName = QFileDialog::getSaveFileName(this,
			tr("Save AEO Light Project"), savDir,
			tr("AEO Settings Files (*.aeo)"));

	if(fileName.isEmpty()) return;

	saveproject(fileName);
	this->prevProjectDir = QFileInfo(fileName).absolutePath();
}

void MainWindow::on_loadprojectButton_clicked()
{
	QString prjDir;

	if(this->prevProjectDir.isEmpty())
	{
		QSettings settings;
		settings.beginGroup("default-folder");
		prjDir = settings.value("project").toString();
		if(prjDir.isEmpty())
		{
			prjDir = QStandardPaths::writableLocation(
						QStandardPaths::DocumentsLocation);
		}
		settings.endGroup();
	}
	else
	{
		prjDir = this->prevProjectDir;
	}

	QString fileName = QFileDialog::getOpenFileName(this,
			tr("Open AEO Light Project"), prjDir,
			tr("AEO Settings Files (*.aeo)"));

	if(fileName.isEmpty()) return;

	this->OpenProject(fileName);
	this->prevProjectDir = QFileInfo(fileName).absolutePath();
}

void MainWindow::on_loadSettingsButton_clicked()
{
	QString prjDir;

	if(this->prevProjectDir.isEmpty())
	{
		QSettings settings;
		settings.beginGroup("default-folder");
		prjDir = settings.value("project").toString();
		if(prjDir.isEmpty())
		{
			prjDir = QStandardPaths::writableLocation(
						QStandardPaths::DocumentsLocation);
		}
		settings.endGroup();
	}
	else
	{
		prjDir = this->prevProjectDir;
	}

	QString fileName = QFileDialog::getOpenFileName(this,
			tr("Open AEO Light Project"), prjDir,
			tr("AEO Settings Files (*.aeo)"));

	if(fileName.isEmpty()) return;

	this->LoadProjectSettings(fileName);
	this->prevProjectDir = QFileInfo(fileName).absolutePath();
}

void MainWindow::on_loadLastSettingsButton_clicked()
{
	LoadLastUsedSettingsSnapshot();
}

void MainWindow::on_queueSavePresetButton_clicked()
{
	SaveQueuePreset();
}

void MainWindow::on_queueLoadPresetButton_clicked()
{
	LoadQueuePreset();
}

void MainWindow::on_queueDeletePresetButton_clicked()
{
	DeleteQueuePreset();
}


//*********************Sequence & Track Selection UI **************************
void MainWindow::on_leftPixSlider_sliderMoved(int position)
{
	ui->leftPixSpinBox->setValue(position);
}

void MainWindow::on_rightPixSlider_sliderMoved(int position)
{
	ui->rightPixSpinBox->setValue(position);
}

void MainWindow::on_leftPixSpinBox_valueChanged(int arg1)
{
	ui->leftPixSlider->setValue(arg1);
	GPU_Params_Update(1);
}

void MainWindow::on_rightPixSpinBox_valueChanged(int arg1)
{
	ui->rightPixSlider->setValue(arg1);
	GPU_Params_Update(1);
}

void MainWindow::on_leftSpinBox_valueChanged(int arg1)
{
	ui->leftSlider->setValue(arg1);
	GPU_Params_Update(1);
}

void MainWindow::on_rightSpinBox_valueChanged(int arg1)
{
	ui->rightSlider->setValue(arg1);
	GPU_Params_Update(1);
}
void MainWindow::on_leftSlider_sliderMoved(int position)
{
	ui->leftSpinBox->setValue(position);
}
void MainWindow::on_rightSlider_sliderMoved(int position)
{
	ui->rightSpinBox->setValue(position);
}
void MainWindow::on_playSlider_sliderMoved(int position)
{
	FrameTexture *ft;

	if(position > ui->frame_numberSpinBox->minimum())
	{
		Load_Frame_Texture(position-1);
	}
	Load_Frame_Texture(position);
	ui->frame_numberSpinBox->setValue(position);
	ui->frameNumberTimeCodeLabel->setText( Compute_Timecode_String(position));

}
void MainWindow::on_markinButton_clicked()
{
	ui->frameInSpinBox->setValue(
			this->scan.inFile.FirstFrame()+ui->playSlider->value());
}

void MainWindow::on_markoutButton_clicked()
{
	ui->frameOutSpinBox->setValue(
			this->scan.inFile.FirstFrame()+ui->playSlider->value());

}

void MainWindow::on_framepitchstartSlider_valueChanged(int value)
{
	NormalizePitchControls(true);
	ui->framepitchstartLabel->setText(
			QString::number(ui->framepitchstartSlider->value()/1000.f,'f',3));
	ui->framePitchLabel->setText(
			QString::number(ui->FramePitchendSlider->value()/1000.f,'f',3));
	GPU_Params_Update(1);
}

void MainWindow::on_videoFrameTopSlider_valueChanged(int value)
{
	NormalizeVideoCropControls(true);
	UpdateVideoCropLabels();
	GPU_Params_Update(1);
}

void MainWindow::on_videoFrameBottomSlider_valueChanged(int value)
{
	NormalizeVideoCropControls(false);
	UpdateVideoCropLabels();
	GPU_Params_Update(1);
}

void MainWindow::on_videoStableCropCheckBox_clicked(bool checked)
{
	videoUseStableFrameCrop = checked;
	UpdateVideoStabilizationControls();
}

void MainWindow::on_videoDriftSensitivitySlider_valueChanged(int value)
{
	videoDriftSensitivityFactor = std::max(0.1f, value / 100.0f);
	UpdateVideoStabilizationControls();
}

void MainWindow::on_overlapSlider_valueChanged(int value)
{
	ui->overlap_label->setText(QString::number(value/100.f,'f',2));
	GPU_Params_Update(1);
}

void MainWindow::NormalizePitchControls(bool startChanged)
{
	if(pitchControlSyncLock) return;
	pitchControlSyncLock = true;

	const int minTick = ui->framepitchstartSlider->minimum();
	const int maxTick = ui->framepitchstartSlider->maximum();
	const int minPitchWindowTicks = 20; // keep at least 2% pitch window
	// Pitch sliders are stored in thousandths of frame height.
	// Clamp against full-frame span (1.0 = 1000), not slider max (0.35).
	const int maxCombinedTicks = 1000 - minPitchWindowTicks;

	int start = ui->framepitchstartSlider->value();
	int end = ui->FramePitchendSlider->value();

	start = std::max(minTick, std::min(maxTick, start));
	end = std::max(minTick, std::min(maxTick, end));

	if(start + end > maxCombinedTicks)
	{
		// Keep the opposite control stable while fine-tuning.
		if(startChanged)
			start = maxCombinedTicks - end;
		else
			end = maxCombinedTicks - start;
	}

	start = std::max(minTick, std::min(maxTick, start));
	end = std::max(minTick, std::min(maxTick, end));

	{
		QSignalBlocker lockStart(ui->framepitchstartSlider);
		QSignalBlocker lockEnd(ui->FramePitchendSlider);
		ui->framepitchstartSlider->setValue(start);
		ui->FramePitchendSlider->setValue(end);
	}

	pitchControlSyncLock = false;
}

void MainWindow::NormalizeVideoCropControls(bool topChanged)
{
	if(videoCropControlSyncLock) return;
	videoCropControlSyncLock = true;

	const int minTick = ui->videoFrameTopSlider->minimum();
	const int maxTick = ui->videoFrameTopSlider->maximum();
	const int minCropWindowTicks = 20; // keep at least 2% visible frame
	const int maxCombinedTicks = maxTick - minCropWindowTicks;

	int top = ui->videoFrameTopSlider->value();
	int bottom = ui->videoFrameBottomSlider->value();

	top = std::max(minTick, std::min(maxTick, top));
	bottom = std::max(minTick, std::min(maxTick, bottom));

	if(top + bottom > maxCombinedTicks)
	{
		// Clamp only the slider being dragged so the opposite control
		// stays stable for fine framing adjustments.
		if(topChanged)
			top = maxCombinedTicks - bottom;
		else
			bottom = maxCombinedTicks - top;
	}

	top = std::max(minTick, std::min(maxTick, top));
	bottom = std::max(minTick, std::min(maxTick, bottom));

	{
		QSignalBlocker lockTop(ui->videoFrameTopSlider);
		QSignalBlocker lockBottom(ui->videoFrameBottomSlider);
		ui->videoFrameTopSlider->setValue(top);
		ui->videoFrameBottomSlider->setValue(bottom);
	}

	videoCropControlSyncLock = false;
}

void MainWindow::UpdateVideoCropLabels()
{
	ui->videoFrameTopLabel->setText(
			QString::number(ui->videoFrameTopSlider->value()/1000.f,'f',2));
	ui->videoFrameBottomLabel->setText(
			QString::number(ui->videoFrameBottomSlider->value()/1000.f,'f',2));
}

void MainWindow::UpdateVideoStabilizationControls()
{
	int sliderValue = ui->videoDriftSensitivitySlider->value();
	sliderValue = std::max(ui->videoDriftSensitivitySlider->minimum(),
			std::min(ui->videoDriftSensitivitySlider->maximum(), sliderValue));

	videoDriftSensitivityFactor = std::max(0.1f, sliderValue / 100.0f);
	videoUseStableFrameCrop = ui->videoStableCropCheckBox->isChecked();

	ui->videoDriftSensitivityLabel->setText(
			QString("%1x").arg(videoDriftSensitivityFactor, 0, 'f', 2));

	const bool adaptiveMode = !videoUseStableFrameCrop;
	ui->videoDriftSensitivitySlider->setEnabled(adaptiveMode);
	ui->videoDriftSensitivityLabel->setEnabled(adaptiveMode);
}

void MainWindow::UpdateExtractOutputControls()
{
	const bool outputVideo = ui->outputVideoCheckBox->isChecked();
	ui->videoOutputFilenameEdit->setEnabled(outputVideo);
	ui->videoOutputBrowseButton->setEnabled(outputVideo);
	ui->outputVideoSizeLabel->setEnabled(outputVideo);
	ui->outputVideoSizeComboBox->setEnabled(outputVideo);
	ui->videoAvOffsetCheckBox->setEnabled(outputVideo);
	ui->outputFlipHorizontalCheckBox->setEnabled(outputVideo);
	ui->outputFlipVerticalCheckBox->setEnabled(outputVideo);
	ui->outputRotationComboBox->setEnabled(outputVideo);
	ui->outputVideoOrientationPresetButton->setEnabled(outputVideo);
}

void MainWindow::InstallInlineHelpIcons()
{
	const QList<QLabel*> labels = findChildren<QLabel*>();
	for(QLabel *label : labels)
	{
		if(!label) continue;
		if(!label->objectName().contains("HelpLabel"))
			continue;
		const QString helpText = label->text().trimmed();
		if(helpText.isEmpty())
			continue;
		label->setToolTip(helpText);
		label->setWhatsThis(helpText);
		label->setText("i");
		label->setWordWrap(false);
		label->setMinimumWidth(16);
		label->setMaximumWidth(16);
		label->setAlignment(Qt::AlignCenter);
		label->setStyleSheet(
				"QLabel {"
				" color: #6f7f93;"
				" border: 1px solid #9fb0c5;"
				" border-radius: 8px;"
				" font-weight: 700;"
				" padding: 0px;"
				"}");
	}
}

void MainWindow::UpdateHelpTextVisibility(bool visible, bool persistSetting)
{
	const QList<QLabel*> labels = findChildren<QLabel*>();
	for(QLabel *label : labels)
	{
		if(!label) continue;
		if(label->objectName().contains("HelpLabel"))
			label->setVisible(visible);
	}

	if(!persistSetting)
		return;

	QSettings settings;
	settings.beginGroup("ui");
	settings.setValue("show-help-text", visible);
	settings.endGroup();
}

void MainWindow::ApplyCompactModePreference()
{
	compactModeEnabled = ReadCompactModeSetting();
	QWidget *cw = centralWidget();
	if(cw)
	{
		if(compactModeEnabled)
		{
			cw->setStyleSheet(
					"QLabel { margin: 0px; }"
					"QPushButton, QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {"
					" min-height: 20px;"
					" padding-top: 0px;"
					" padding-bottom: 0px;"
					"}"
					"QGroupBox { margin-top: 6px; }");
		}
		else
		{
			cw->setStyleSheet(QString());
		}
	}

	const bool showHelpText = ReadShowHelpTextSetting();
	UpdateHelpTextVisibility(compactModeEnabled ? false : showHelpText, false);
}

void MainWindow::ApplyVideoContainerExtensionToOutputFilename()
{
	QString current = ui->videoOutputFilenameEdit->text().trimmed();
	if(current.isEmpty()) return;

	QFileInfo info(current);
	QString adjusted = info.path() + "/" + info.completeBaseName() +
			VideoContainerExtensionForMode(videoContainerMode);
	ui->videoOutputFilenameEdit->setText(adjusted);
}

void MainWindow::LoadExtractOutputSettings()
{
	QSettings settings;
	settings.beginGroup("extraction");
	ui->audioOutputFilenameEdit->setText(
			settings.value("output-audio-file", "").toString());
	ui->outputVideoCheckBox->setChecked(
			settings.value("output-video-enabled", false).toBool());
	ui->videoOutputFilenameEdit->setText(
			settings.value("output-video-file", "").toString());
	ui->outputFilenameTemplateEdit->setText(
			settings.value("output-name-template", "{source}_{datetime}").toString());
	outputConflictMode = ClampRangeInt(
			settings.value("output-conflict-mode", 0).toInt(), 0, 1);
	ui->outputConflictModeComboBox->setCurrentIndex(outputConflictMode);
	outputOpenFolderAfterExport = settings.value("output-open-folder", false).toBool();
	ui->outputOpenFolderCheckBox->setChecked(outputOpenFolderAfterExport);
	autoCleanupTempFilesEnabled =
			settings.value("output-auto-cleanup-temp", true).toBool();
	ui->outputAutoCleanupTempCheckBox->setChecked(autoCleanupTempFilesEnabled);
	exportReportMode = ClampRangeInt(
			settings.value("output-export-report-mode", EXPORT_REPORT_OFF).toInt(),
			EXPORT_REPORT_OFF, EXPORT_REPORT_BOTH);
	ui->outputExportReportModeComboBox->setCurrentIndex(exportReportMode);
	queueBatchReportMode = ClampRangeInt(
			settings.value("queue-batch-report-mode", QUEUE_BATCH_REPORT_OFF).toInt(),
			QUEUE_BATCH_REPORT_OFF, QUEUE_BATCH_REPORT_BOTH);
	ui->queueBatchReportModeComboBox->setCurrentIndex(queueBatchReportMode);
	ui->videoAvOffsetCheckBox->setChecked(
			settings.value("video-av-offset", true).toBool());
	ui->outputFlipHorizontalCheckBox->setChecked(
			settings.value("video-flip-h", false).toBool());
	ui->outputFlipVerticalCheckBox->setChecked(
			settings.value("video-flip-v", false).toBool());
	ui->outputRotationComboBox->setCurrentIndex(ClampRangeInt(
			settings.value("video-rotation-index", 0).toInt(), 0, 3));
	settings.endGroup();
	UpdateExtractOutputControls();
	ApplyVideoContainerExtensionToOutputFilename();
}

void MainWindow::SaveExtractOutputSettings() const
{
	QSettings settings;
	settings.beginGroup("extraction");
	settings.setValue("output-audio-file", ui->audioOutputFilenameEdit->text());
	settings.setValue("output-video-enabled", ui->outputVideoCheckBox->isChecked());
	settings.setValue("output-video-file", ui->videoOutputFilenameEdit->text());
	settings.setValue("output-name-template", ui->outputFilenameTemplateEdit->text());
	settings.setValue("output-conflict-mode", ui->outputConflictModeComboBox->currentIndex());
	settings.setValue("output-open-folder", ui->outputOpenFolderCheckBox->isChecked());
	settings.setValue(
			"output-auto-cleanup-temp",
			ui->outputAutoCleanupTempCheckBox->isChecked());
	settings.setValue(
			"output-export-report-mode",
			ui->outputExportReportModeComboBox->currentIndex());
	settings.setValue(
			"queue-batch-report-mode",
			ui->queueBatchReportModeComboBox->currentIndex());
	settings.setValue("video-av-offset", ui->videoAvOffsetCheckBox->isChecked());
	settings.setValue("video-flip-h", ui->outputFlipHorizontalCheckBox->isChecked());
	settings.setValue("video-flip-v", ui->outputFlipVerticalCheckBox->isChecked());
	settings.setValue("video-rotation-index", ui->outputRotationComboBox->currentIndex());
	settings.endGroup();
}

void MainWindow::LoadVideoExportSettings()
{
	QSettings settings;
	settings.beginGroup("video-export");
	videoExportMode = ClampRangeInt(
			settings.value("simple-mode", SIMPLE_EXPORT_STANDARD).toInt(),
			SIMPLE_EXPORT_FAST, SIMPLE_EXPORT_ULTRA);
	videoContainerMode = ClampRangeInt(
			settings.value("container", VIDEO_CONTAINER_MP4).toInt(),
			VIDEO_CONTAINER_MP4, VIDEO_CONTAINER_MKV);
	videoCodecMode = ClampRangeInt(
			settings.value("codec", VIDEO_CODEC_H264).toInt(),
			VIDEO_CODEC_H264, VIDEO_CODEC_MPEG4);
	videoResolutionMode = ClampRangeInt(
			settings.value("resolution-mode", VIDEO_RESOLUTION_SOURCE).toInt(),
			VIDEO_RESOLUTION_SOURCE, VIDEO_RESOLUTION_3840X2160);
	videoQualityPercent = ClampRangeInt(
			settings.value("quality-percent", 85).toInt(), 1, 100);
	videoAudioBitrateKbps = ClampRangeInt(
			settings.value("audio-bitrate-kbps", 192).toInt(), 96, 512);
	videoPresetMode = ClampRangeInt(
			settings.value("preset-mode", 1).toInt(), 0, 2);
	autoOptimizeFramesEnabled =
			settings.value("auto-optimize-frames", false).toBool();
	autoOptimizeVideoEnabled =
			settings.value("auto-optimize-video", false).toBool();
	autoOptimizeColorEnabled =
			settings.value("auto-optimize-color", true).toBool();
	autoOptimizeProfileMode = ClampRangeInt(
			settings.value("auto-optimize-profile", 1).toInt(), 0, 2);
	settings.endGroup();
	UpdateVideoExportControls();
}

void MainWindow::SaveVideoExportSettings() const
{
	QSettings settings;
	settings.beginGroup("video-export");
	settings.setValue("simple-mode", videoExportMode);
	settings.setValue("container", videoContainerMode);
	settings.setValue("codec", videoCodecMode);
	settings.setValue("resolution-mode", videoResolutionMode);
	settings.setValue("quality-percent", videoQualityPercent);
	settings.setValue("audio-bitrate-kbps", videoAudioBitrateKbps);
	settings.setValue("preset-mode", videoPresetMode);
	settings.setValue("auto-optimize-frames", autoOptimizeFramesEnabled);
	settings.setValue("auto-optimize-video", autoOptimizeVideoEnabled);
	settings.setValue("auto-optimize-color", autoOptimizeColorEnabled);
	settings.setValue("auto-optimize-profile", autoOptimizeProfileMode);
	settings.endGroup();
}

void MainWindow::ApplyVideoTargetProfile(int profileMode)
{
	switch(ClampRangeInt(
			profileMode,
			VIDEO_TARGET_PROFILE_CUSTOM,
			VIDEO_TARGET_PROFILE_ARCHIVE))
	{
	case VIDEO_TARGET_PROFILE_QUICK_REVIEW:
		videoExportMode = SIMPLE_EXPORT_FAST;
		videoContainerMode = VIDEO_CONTAINER_MP4;
		videoCodecMode = VIDEO_CODEC_H264;
		videoResolutionMode = VIDEO_RESOLUTION_1280X720;
		videoPresetMode = 0;
		videoQualityPercent = 60;
		videoAudioBitrateKbps = 128;
		break;
	case VIDEO_TARGET_PROFILE_WEB_SHARE:
		videoExportMode = SIMPLE_EXPORT_STANDARD;
		videoContainerMode = VIDEO_CONTAINER_MP4;
		videoCodecMode = VIDEO_CODEC_H264;
		videoResolutionMode = VIDEO_RESOLUTION_1920X1080;
		videoPresetMode = 1;
		videoQualityPercent = 76;
		videoAudioBitrateKbps = 160;
		break;
	case VIDEO_TARGET_PROFILE_ARCHIVE:
		videoExportMode = SIMPLE_EXPORT_ULTRA;
		videoContainerMode = VIDEO_CONTAINER_MOV;
		videoCodecMode = VIDEO_CODEC_H264;
		videoResolutionMode = VIDEO_RESOLUTION_SOURCE;
		videoPresetMode = 2;
		videoQualityPercent = 95;
		videoAudioBitrateKbps = 320;
		break;
	case VIDEO_TARGET_PROFILE_CUSTOM:
	default:
		break;
	}
}

void MainWindow::UpdateVideoTargetProfileFromControls()
{
	videoTargetProfileMode = VIDEO_TARGET_PROFILE_CUSTOM;
	if(videoExportMode == SIMPLE_EXPORT_FAST &&
			videoContainerMode == VIDEO_CONTAINER_MP4 &&
			videoCodecMode == VIDEO_CODEC_H264 &&
			videoResolutionMode == VIDEO_RESOLUTION_1280X720 &&
			videoPresetMode == 0 &&
			videoQualityPercent == 60 &&
			videoAudioBitrateKbps == 128)
	{
		videoTargetProfileMode = VIDEO_TARGET_PROFILE_QUICK_REVIEW;
		return;
	}
	if(videoExportMode == SIMPLE_EXPORT_STANDARD &&
			videoContainerMode == VIDEO_CONTAINER_MP4 &&
			videoCodecMode == VIDEO_CODEC_H264 &&
			videoResolutionMode == VIDEO_RESOLUTION_1920X1080 &&
			videoPresetMode == 1 &&
			videoQualityPercent == 76 &&
			videoAudioBitrateKbps == 160)
	{
		videoTargetProfileMode = VIDEO_TARGET_PROFILE_WEB_SHARE;
		return;
	}
	if(videoExportMode == SIMPLE_EXPORT_ULTRA &&
			videoContainerMode == VIDEO_CONTAINER_MOV &&
			videoCodecMode == VIDEO_CODEC_H264 &&
			videoResolutionMode == VIDEO_RESOLUTION_SOURCE &&
			videoPresetMode == 2 &&
			videoQualityPercent == 95 &&
			videoAudioBitrateKbps == 320)
	{
		videoTargetProfileMode = VIDEO_TARGET_PROFILE_ARCHIVE;
	}
}

void MainWindow::UpdateVideoExportControls()
{
	UpdateVideoTargetProfileFromControls();

	QSignalBlocker blockMode(ui->videoExportModeComboBox);
	QSignalBlocker blockTargetProfile(ui->videoExportTargetProfileComboBox);
	QSignalBlocker blockContainer(ui->videoExportContainerComboBox);
	QSignalBlocker blockCodec(ui->videoExportCodecComboBox);
	QSignalBlocker blockResolution(ui->videoExportResolutionComboBox);
	QSignalBlocker blockPreset(ui->videoExportPresetComboBox);
	QSignalBlocker blockQuality(ui->videoExportQualitySlider);
	QSignalBlocker blockAudio(ui->videoExportAudioBitrateSpinBox);
	QSignalBlocker blockQuickResolution(ui->outputVideoSizeComboBox);
	QSignalBlocker blockAutoFrames(ui->autoOptimizeFramesCheckBox);
	QSignalBlocker blockAutoVideo(ui->autoOptimizeVideoCheckBox);
	QSignalBlocker blockAutoColor(ui->autoOptimizeColorCheckBox);
	QSignalBlocker blockAutoProfile(ui->autoOptimizeProfileComboBox);

	ui->videoExportModeComboBox->setCurrentIndex(
			ClampRangeInt(videoExportMode, SIMPLE_EXPORT_FAST, SIMPLE_EXPORT_ULTRA));
	ui->videoExportTargetProfileComboBox->setCurrentIndex(
			ClampRangeInt(
					videoTargetProfileMode,
					VIDEO_TARGET_PROFILE_CUSTOM,
					VIDEO_TARGET_PROFILE_ARCHIVE));
	ui->videoExportContainerComboBox->setCurrentIndex(
			ClampRangeInt(videoContainerMode, VIDEO_CONTAINER_MP4, VIDEO_CONTAINER_MKV));
	ui->videoExportCodecComboBox->setCurrentIndex(
			ClampRangeInt(videoCodecMode, VIDEO_CODEC_H264, VIDEO_CODEC_MPEG4));
	ui->videoExportResolutionComboBox->setCurrentIndex(
			ClampRangeInt(videoResolutionMode,
					VIDEO_RESOLUTION_SOURCE, VIDEO_RESOLUTION_3840X2160));
	ui->outputVideoSizeComboBox->setCurrentIndex(
			ClampRangeInt(videoResolutionMode,
					VIDEO_RESOLUTION_SOURCE, VIDEO_RESOLUTION_3840X2160));
	ui->videoExportPresetComboBox->setCurrentIndex(
			ClampRangeInt(videoPresetMode, 0, 2));
	ui->videoExportQualitySlider->setValue(
			ClampRangeInt(videoQualityPercent, 1, 100));
	ui->videoExportAudioBitrateSpinBox->setValue(
			ClampRangeInt(videoAudioBitrateKbps, 96, 512));
	ui->autoOptimizeFramesCheckBox->setChecked(autoOptimizeFramesEnabled);
	ui->autoOptimizeVideoCheckBox->setChecked(autoOptimizeVideoEnabled);
	ui->autoOptimizeColorCheckBox->setChecked(autoOptimizeColorEnabled);
	ui->autoOptimizeProfileComboBox->setCurrentIndex(
			ClampRangeInt(autoOptimizeProfileMode, 0, 2));

	static const char *modeNames[] = {
		"Snel", "Standaard", "Kwaliteit", "Ultra kwaliteit"
	};
	static const char *containerNames[] = {
		"MP4", "MOV", "MKV"
	};
	static const char *codecNames[] = {
		"H.264", "HEVC", "MPEG-4"
	};
	static const char *profileNames[] = {
		"Aangepast",
		"Snelle review",
		"Web share",
		"Archiefkwaliteit"
	};
	static const char *presetNames[] = {
		"Sneller", "Gebalanceerd", "Langzamer"
	};

	ui->videoExportQualityValueLabel->setText(
			QString("%1%").arg(videoQualityPercent));
	QString summaryText = tr("Profiel: %1.\nActief: %2, %3, %4, %5, preset %6, kwaliteit %7%, audio %8 kbps.")
			.arg(profileNames[ClampRangeInt(videoTargetProfileMode, 0, 3)])
			.arg(modeNames[ClampRangeInt(videoExportMode, 0, 3)])
			.arg(containerNames[ClampRangeInt(videoContainerMode, 0, 2)])
			.arg(codecNames[ClampRangeInt(videoCodecMode, 0, 2)])
			.arg(VideoResolutionLabelForMode(videoResolutionMode))
			.arg(presetNames[ClampRangeInt(videoPresetMode, 0, 2)])
			.arg(videoQualityPercent)
			.arg(videoAudioBitrateKbps);
	QStringList warnings;
	if(videoExportMode == SIMPLE_EXPORT_FAST && videoQualityPercent < 35)
	{
		warnings << tr("Fast + lage kwaliteit kan zichtbare compressie-artefacten geven.");
	}
	if(videoCodecMode == VIDEO_CODEC_MPEG4 &&
			videoResolutionMode >= VIDEO_RESOLUTION_1920X1080)
	{
		warnings << tr("MPEG-4 op HD/4K geeft vaak grotere bestanden met lagere kwaliteit.");
	}
	if(videoAudioBitrateKbps < 128)
	{
		warnings << tr("Audio bitrate < 128 kbps kan hoorbaar kwaliteitsverlies geven.");
	}
	const double sourceFpsHint = this->scan.inFile.SourceFpsHint();
	const double selectedFps = ReadFpsFromUiIndex(ui->filmrate_PD->currentIndex());
	if(sourceFpsHint > 0.0 && std::fabs(sourceFpsHint - selectedFps) > 0.05)
	{
		warnings << tr("FPS mismatch: bron ~%1 fps, ingestelde frame rate %2 fps.")
				.arg(sourceFpsHint, 0, 'f', 3)
				.arg(selectedFps, 0, 'f', 3);
	}
	if(ui->outputVideoCheckBox->isChecked())
	{
		const QString videoPath = ui->videoOutputFilenameEdit->text().trimmed();
		if(videoPath.isEmpty())
		{
			warnings << tr("Video-outputpad ontbreekt. Kies een outputbestand voor video-export.");
		}
		else
		{
			const QString expectedExt =
					VideoContainerExtensionForMode(videoContainerMode).mid(1).toLower();
			const QString actualExt = QFileInfo(videoPath).suffix().toLower();
			if(!actualExt.isEmpty() && actualExt != expectedExt)
			{
				warnings << tr("Container (%1) en bestands-extensie (%2) komen niet overeen.")
						.arg(containerNames[ClampRangeInt(videoContainerMode, 0, 2)])
						.arg(actualExt.toUpper());
			}
		}
	}
	if(!warnings.isEmpty())
	{
		summaryText += tr("\nWaarschuwing: %1").arg(warnings.join(" "));
	}
	ui->videoExportSummaryLabel->setText(summaryText);
	ui->videoExportSummaryLabel->setStyleSheet(warnings.isEmpty() ?
			QString() :
			"QLabel { color: #b86b00; font-weight: 600; }");
#ifdef USELIBAV
	ui->videoExportFfmpegStatusLabel->setText(
			tr("Encoder: ingebouwde LibAV (externe FFmpeg niet vereist)."));
#else
	const QString ffmpegBin = FindExternalFfmpeg();
	if(ffmpegBin.isEmpty())
	{
		ui->videoExportFfmpegStatusLabel->setText(
				tr("FFmpeg niet gevonden. Video-export werkt pas nadat FFmpeg beschikbaar is."));
	}
	else
	{
		ui->videoExportFfmpegStatusLabel->setText(
				tr("FFmpeg pad: %1").arg(QDir::toNativeSeparators(ffmpegBin)));
	}
#endif
	UpdateAutoOptimizationControls();
}

void MainWindow::UpdateAutoOptimizationControls()
{
	ui->autoOptimizeColorCheckBox->setEnabled(
			autoOptimizeFramesEnabled || autoOptimizeVideoEnabled);
	ui->autoOptimizeProfileComboBox->setEnabled(
			autoOptimizeFramesEnabled || autoOptimizeVideoEnabled);
	ui->autoOptimizeAnalyzeButton->setEnabled(
			currentFrameTexture != nullptr &&
			(autoOptimizeFramesEnabled || autoOptimizeVideoEnabled));

	QString summary = tr("Auto-optimalisatie uit.");
	if(autoOptimizeFramesEnabled || autoOptimizeVideoEnabled)
	{
		double meanLuma = 0.0;
		double lumaLow = 0.0;
		double lumaHigh = 0.0;
		double spread = 0.0;
		if(AnalyzeCurrentFrameAutoStats(meanLuma, lumaLow, lumaHigh, spread))
		{
			const double range = std::max(0.01, lumaHigh - lumaLow);
			summary = tr("Analyse huidig frame: luma %1, bereik %2.")
					.arg(meanLuma, 0, 'f', 2)
					.arg(range, 0, 'f', 2);
			if(autoOptimizeVideoEnabled)
			{
				double b = 0.0, c = 1.0, s = 1.0;
				QString videoSummary;
				if(ComputeAutoVideoEqSettings(b, c, s, &videoSummary))
					summary += QString(" ") + videoSummary;
			}
		}
		else
		{
			summary = tr("Auto-optimalisatie actief, maar frame-analyse is nog niet beschikbaar.");
		}
	}
	ui->autoOptimizeSummaryLabel->setText(summary);
}

QString MainWindow::RunFfmpegSelfTest(QString *errorOut) const
{
	if(errorOut) errorOut->clear();
#ifdef USELIBAV
	return tr("Ingebouwde LibAV encoder actief (geen externe FFmpeg vereist).");
#else
	const QString ffmpegBin = FindExternalFfmpeg();
	if(ffmpegBin.isEmpty())
	{
		if(errorOut)
		{
			*errorOut = tr("FFmpeg uitvoerbaar bestand niet gevonden in PATH.\n"
					"Installeer FFmpeg of plaats ffmpeg.exe naast AEO-Light.");
		}
		return QString();
	}

	QProcess ffmpeg;
	ffmpeg.setProcessChannelMode(QProcess::MergedChannels);
	ffmpeg.start(ffmpegBin, QStringList() << "-hide_banner" << "-version");
	if(!ffmpeg.waitForStarted(5000))
	{
		if(errorOut)
			*errorOut = tr("FFmpeg kon niet starten via: %1")
					.arg(QDir::toNativeSeparators(ffmpegBin));
		return QString();
	}
	if(!ffmpeg.waitForFinished(6000))
	{
		ffmpeg.kill();
		ffmpeg.waitForFinished(1000);
		if(errorOut)
			*errorOut = tr("FFmpeg test time-out. Controleer de installatie.");
		return QString();
	}

	const QString output = QString::fromLocal8Bit(ffmpeg.readAll()).trimmed();
	if(ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0)
	{
		if(errorOut)
		{
			*errorOut = tr("FFmpeg reageerde met een foutcode (%1).\n%2")
					.arg(ffmpeg.exitCode())
					.arg(output.left(800));
		}
		return QString();
	}

	QString firstLine;
	const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
	for(const QString &line : lines)
	{
		const QString trimmed = line.trimmed();
		if(!trimmed.isEmpty())
		{
			firstLine = trimmed;
			break;
		}
	}
	if(firstLine.isEmpty())
		firstLine = tr("FFmpeg reageerde zonder versie-output.");
	return firstLine;
#endif
}

bool MainWindow::CheckExportDiskSpace(const QString &audioPath, const QString &videoPath,
		long numFrames, QString *detailsOut) const
{
	if(detailsOut) detailsOut->clear();
	if(numFrames <= 0 || audioPath.trimmed().isEmpty())
		return true;

	const double fps = ReadFpsFromUiIndex(ui->filmrate_PD->currentIndex());
	const int sampleRate = (ui->filerate_PD->currentIndex() == 1) ? 96000 : 48000;
	const int bitDepth = ui->bitDepthComboBox->currentText().split(" ")[0].toInt();
	const int channels = 2;
	const double seconds = std::max(0.01, static_cast<double>(numFrames) / std::max(1.0, fps));
	const double bytesPerSecond = sampleRate * channels * std::max(1, bitDepth) / 8.0;
	const qint64 audioBytes = std::max<qint64>(
			2 * 1024 * 1024,
			static_cast<qint64>(seconds * bytesPerSecond + 2 * 1024 * 1024));

	qint64 videoBytes = 0;
	qint64 tempBytes = 0;
	if(!videoPath.trimmed().isEmpty())
	{
		QSize estimatedSize = VideoResolutionSizeForMode(videoResolutionMode);
		if(estimatedSize.width() <= 0 || estimatedSize.height() <= 0)
		{
			if(currentFrameTexture &&
					currentFrameTexture->width > 0 &&
					currentFrameTexture->height > 0)
			{
				estimatedSize = QSize(
						std::max(320, currentFrameTexture->width),
						std::max(240, currentFrameTexture->height));
			}
			else
			{
				estimatedSize = QSize(1920, 1080);
			}
		}

		double bitsPerPixel = 0.22;
		switch(videoExportMode)
		{
		case SIMPLE_EXPORT_FAST: bitsPerPixel = 0.14; break;
		case SIMPLE_EXPORT_QUALITY: bitsPerPixel = 0.30; break;
		case SIMPLE_EXPORT_ULTRA: bitsPerPixel = 0.45; break;
		default: bitsPerPixel = 0.22; break;
		}
		if(videoCodecMode == VIDEO_CODEC_HEVC) bitsPerPixel *= 0.75;
		if(videoCodecMode == VIDEO_CODEC_MPEG4) bitsPerPixel *= 1.25;

		const long double totalPixels = static_cast<long double>(estimatedSize.width()) *
				static_cast<long double>(estimatedSize.height()) *
				static_cast<long double>(std::max<long>(1, numFrames));
		videoBytes = static_cast<qint64>(totalPixels * bitsPerPixel / 8.0L) +
				16 * 1024 * 1024;

#ifndef USELIBAV
		// External FFmpeg workflow writes temporary PNG frames before muxing.
		tempBytes = static_cast<qint64>(totalPixels * 1.2L) + 64 * 1024 * 1024;
#endif
	}

	QHash<QString, qint64> neededByRoot;
	auto addNeed = [&](const QString &targetPath, qint64 bytes)
	{
		if(targetPath.trimmed().isEmpty() || bytes <= 0) return;
		const QString absolutePath = QFileInfo(targetPath).absolutePath();
		QStorageInfo storage(absolutePath);
		QString root = storage.rootPath();
		if(root.isEmpty())
			root = absolutePath;
		neededByRoot[root] += bytes;
	};

	addNeed(audioPath, audioBytes);
	addNeed(videoPath, videoBytes);
#ifndef USELIBAV
	if(!videoPath.trimmed().isEmpty() && tempBytes > 0)
	{
		const QString tempPlaceholder = QDir::temp().filePath("aeo-temp-space-check.tmp");
		addNeed(tempPlaceholder, tempBytes);
	}
#endif

	const qint64 safetyMargin = 128 * 1024 * 1024;
	bool enoughSpace = true;
	QStringList lines;
	lines << tr("Geschatte ruimte: audio %1, video %2, tijdelijk %3.")
			.arg(FormatBytesHumanReadable(audioBytes))
			.arg(FormatBytesHumanReadable(videoBytes))
			.arg(FormatBytesHumanReadable(tempBytes));

	for(auto it = neededByRoot.constBegin(); it != neededByRoot.constEnd(); ++it)
	{
		const QString root = it.key();
		const qint64 needed = it.value();
		QStorageInfo storage(root);
		const qint64 available = storage.bytesAvailable();
		const qint64 neededWithMargin = needed + safetyMargin;
		const bool availabilityKnown = (available >= 0);
		const bool rootOk = !availabilityKnown || (available >= neededWithMargin);
		enoughSpace = enoughSpace && rootOk;
		if(availabilityKnown)
		{
			lines << tr("Schijf %1: beschikbaar %2, nodig %3 (incl. marge).")
					.arg(QDir::toNativeSeparators(root))
					.arg(FormatBytesHumanReadable(available))
					.arg(FormatBytesHumanReadable(neededWithMargin));
		}
		else
		{
			lines << tr("Schijf %1: beschikbare ruimte kon niet worden bepaald; geschatte nood %2.")
					.arg(QDir::toNativeSeparators(root))
					.arg(FormatBytesHumanReadable(neededWithMargin));
		}
	}

	if(detailsOut)
		*detailsOut = lines.join("\n");
	return enoughSpace;
}

bool MainWindow::CheckExportIntegrity(const QString &audioPath, const QString &videoPath,
		long numFrames, QString *detailsOut) const
{
	if(detailsOut) detailsOut->clear();
	QStringList issues;
	QStringList info;

	const QFileInfo audioInfo(audioPath);
	if(!audioInfo.exists() || audioInfo.size() <= 44)
	{
		issues << tr("Audio-output ontbreekt of is te klein.");
	}
	else
	{
		const int sampleRate = (ui->filerate_PD->currentIndex() == 1) ? 96000 : 48000;
		const int bitDepth = ui->bitDepthComboBox->currentText().split(" ")[0].toInt();
		const int channels = 2;
		const qint64 payloadBytes = std::max<qint64>(0, audioInfo.size() - 44);
		const double bytesPerSecond = sampleRate * channels * std::max(1, bitDepth) / 8.0;
		const double audioDurationSec = payloadBytes / std::max(1.0, bytesPerSecond);
		info << tr("Audio duur: %1s").arg(audioDurationSec, 0, 'f', 2);
		if(audioDurationSec <= 0.01)
			issues << tr("Audio-output lijkt leeg (duur <= 0).");
		if(numFrames > 0)
		{
			const double expectedDuration = static_cast<double>(numFrames) /
					std::max(1.0, ReadFpsFromUiIndex(ui->filmrate_PD->currentIndex()));
			if(audioDurationSec < expectedDuration * 0.2)
			{
				issues << tr("Audio-outputduur is veel korter dan verwacht.");
			}
		}
	}

	if(!videoPath.trimmed().isEmpty())
	{
		const QFileInfo videoInfo(videoPath);
		if(!videoInfo.exists() || videoInfo.size() <= 1024)
		{
			issues << tr("Video-output ontbreekt of is te klein.");
		}
		else
		{
			info << tr("Video grootte: %1").arg(FormatBytesHumanReadable(videoInfo.size()));
		}
	}

	QString summary;
	if(!info.isEmpty())
		summary += info.join(", ");
	if(!issues.isEmpty())
	{
		if(!summary.isEmpty()) summary += "\n";
		summary += issues.join("\n");
	}
	if(detailsOut) *detailsOut = summary;
	return issues.isEmpty();
}

bool MainWindow::AnalyzeCurrentFrameAutoStats(double &meanLuma, double &lumaLow,
		double &lumaHigh, double &channelSpread) const
{
	AutoFrameStats stats;
	if(!ComputeAutoFrameStats(currentFrameTexture, stats))
		return false;
	meanLuma = stats.meanLuma;
	lumaLow = stats.lumaLow;
	lumaHigh = stats.lumaHigh;
	channelSpread = stats.channelSpread;
	return true;
}

void MainWindow::ApplyAutoOptimizationToFrameControls(bool forceApply)
{
	if(autoOptimizeFrameApplyLock)
		return;
	if(!forceApply && !autoOptimizeFramesEnabled)
		return;
	if(frame_window == NULL || !currentFrameTexture)
	{
		UpdateAutoOptimizationControls();
		return;
	}
	if(frame_window->is_rendering)
		return;

	double meanLuma = 0.0;
	double lumaLow = 0.0;
	double lumaHigh = 0.0;
	double channelSpread = 0.0;
	if(!AnalyzeCurrentFrameAutoStats(meanLuma, lumaLow, lumaHigh, channelSpread))
	{
		UpdateAutoOptimizationControls();
		return;
	}

	const double strength = AutoProfileStrength(autoOptimizeProfileMode);
	const double range = std::max(0.01, lumaHigh - lumaLow);
	const double targetMean = 0.52;
	const double targetRange = 0.62;
	const double meanDelta = targetMean - meanLuma;
	const double contrastDelta = targetRange - range;

	const int liftTarget = ClampRangeInt(
			int(std::lround((meanDelta * 120.0 + contrastDelta * 25.0) * strength)),
			-45, 45);
	const int gammaTarget = ClampRangeInt(
			int(std::lround(100.0 - meanDelta * 50.0 * strength)),
			80, 130);
	const int gainTarget = ClampRangeInt(
			int(std::lround(100.0 + contrastDelta * 190.0 * strength)),
			75, 190);

	autoOptimizeFrameApplyLock = true;
	{
		QSignalBlocker lockLift(ui->liftSlider);
		QSignalBlocker lockGamma(ui->gammaSlider);
		QSignalBlocker lockGain(ui->gainSlider);
		ui->liftSlider->setValue(liftTarget);
		ui->gammaSlider->setValue(gammaTarget);
		ui->gainSlider->setValue(gainTarget);
	}
	ui->liftLabel->setText(QString::number(ui->liftSlider->value() / 100.f, 'f', 2));
	ui->gammaLabel->setText(QString::number(ui->gammaSlider->value() / 100.f, 'f', 2));
	ui->gainLabel->setText(QString::number(ui->gainSlider->value() / 100.f, 'f', 2));

	if(autoOptimizeColorEnabled)
	{
		QSignalBlocker lockDesat(ui->desatBox);
		if(ui->desatBox->isChecked())
			ui->desatBox->setChecked(false);
	}
	autoOptimizeFrameApplyLock = false;
	GPU_Params_Update(1);
	UpdateAutoOptimizationControls();
}

bool MainWindow::ComputeAutoVideoEqSettings(double &brightness, double &contrast,
		double &saturation, QString *summary) const
{
	double meanLuma = 0.0;
	double lumaLow = 0.0;
	double lumaHigh = 0.0;
	double channelSpread = 0.0;
	if(!AnalyzeCurrentFrameAutoStats(meanLuma, lumaLow, lumaHigh, channelSpread))
		return false;

	const double strength = AutoProfileStrength(autoOptimizeProfileMode);
	const double range = std::max(0.01, lumaHigh - lumaLow);
	brightness = std::max(-0.20, std::min(0.20, (0.52 - meanLuma) * 0.45 * strength));
	contrast = std::max(0.75, std::min(1.45, 1.0 + (0.62 - range) * 1.35 * strength));
	saturation = 1.0;
	if(autoOptimizeColorEnabled)
	{
		saturation = std::max(0.75, std::min(1.45,
				1.0 + (0.10 - channelSpread) * 2.2 * strength));
	}

	if(summary)
	{
		*summary = tr("Video auto: contrast %1x, helderheid %2, saturatie %3x.")
				.arg(contrast, 0, 'f', 2)
				.arg(brightness, 0, 'f', 2)
				.arg(saturation, 0, 'f', 2);
	}
	return true;
}

void MainWindow::UpdateAudioQualityPresetFromControls()
{
	if(audioQualityPresetSyncLock) return;
	audioQualityPresetSyncLock = true;

	int targetPreset = AUDIO_QUALITY_PRESET_CUSTOM;
	const int samplerateIndex = ui->filerate_PD->currentIndex();
	const int bitDepthIndex = ui->bitDepthComboBox->currentIndex();
	const int bitrateKbps = videoAudioBitrateKbps;

	if(samplerateIndex == 0 && bitDepthIndex == 0 && bitrateKbps == 128)
		targetPreset = AUDIO_QUALITY_PRESET_COMPACT;
	else if(samplerateIndex == 0 && bitDepthIndex == 1 && bitrateKbps == 192)
		targetPreset = AUDIO_QUALITY_PRESET_STANDARD;
	else if(samplerateIndex == 1 && bitDepthIndex == 1 && bitrateKbps == 320)
		targetPreset = AUDIO_QUALITY_PRESET_HIGH;

	if(ui->audioQualityPresetComboBox->currentIndex() != targetPreset)
		ui->audioQualityPresetComboBox->setCurrentIndex(targetPreset);

	audioQualityPresetSyncLock = false;
}

void MainWindow::UpdateAudioOnlyQualityPresetFromControls()
{
	if(audioOnlyQualityPresetSyncLock) return;
	audioOnlyQualityPresetSyncLock = true;

	int targetPreset = AUDIO_QUALITY_PRESET_CUSTOM;
	const int samplerateIndex = ui->filerate_PD->currentIndex();
	const int bitDepthIndex = ui->bitDepthComboBox->currentIndex();

	if(samplerateIndex == 0 && bitDepthIndex == 0)
		targetPreset = AUDIO_QUALITY_PRESET_COMPACT;
	else if(samplerateIndex == 0 && bitDepthIndex == 1)
		targetPreset = AUDIO_QUALITY_PRESET_STANDARD;
	else if(samplerateIndex == 1 && bitDepthIndex == 1)
		targetPreset = AUDIO_QUALITY_PRESET_HIGH;

	if(ui->audioOnlyQualityPresetComboBox->currentIndex() != targetPreset)
		ui->audioOnlyQualityPresetComboBox->setCurrentIndex(targetPreset);

	audioOnlyQualityPresetSyncLock = false;
}

QString MainWindow::BuildOutputNameFromTemplate(const QString &ext) const
{
	QString templ = ui->outputFilenameTemplateEdit->text().trimmed();
	if(templ.isEmpty())
		templ = "{source}_{datetime}";

	QString sourceName = "aeo-light";
	if(this->scan.inFile.IsReady())
	{
		QFileInfo fi(QString::fromStdString(this->scan.inFile.GetFileName()));
		QString base = fi.completeBaseName().trimmed();
		if(!base.isEmpty())
			sourceName = base;
	}

	const QDateTime now = QDateTime::currentDateTime();
	const QString sizeToken = VideoResolutionLabelForMode(videoResolutionMode);
	const QString framesToken = QString("%1-%2")
			.arg(ui->frameInSpinBox->value())
			.arg(ui->frameOutSpinBox->value());

	QString baseName = templ;
	baseName.replace("{source}", sourceName, Qt::CaseInsensitive);
	baseName.replace("{project}", sourceName, Qt::CaseInsensitive);
	baseName.replace("{date}", now.toString("yyyyMMdd"), Qt::CaseInsensitive);
	baseName.replace("{time}", now.toString("HHmmss"), Qt::CaseInsensitive);
	baseName.replace("{datetime}", now.toString("yyyyMMdd_HHmmss"), Qt::CaseInsensitive);
	baseName.replace("{frames}", framesToken, Qt::CaseInsensitive);
	baseName.replace("{size}", sizeToken, Qt::CaseInsensitive);
	baseName = SanitizeFilenamePart(baseName);

	QString safeExt = ext;
	if(!safeExt.isEmpty() && !safeExt.startsWith("."))
		safeExt.prepend(".");
	if(!safeExt.isEmpty() && baseName.endsWith(safeExt, Qt::CaseInsensitive))
		return baseName;
	return baseName + safeExt;
}

void MainWindow::ApplyThemeFromSettings()
{
	QSettings settings;
	settings.beginGroup("ui");
	QString theme = settings.value("theme", "light").toString().toLower();
	settings.endGroup();

	if(theme == "dark")
		qApp->setStyleSheet(BuildDarkThemeStyleSheet());
	else
		qApp->setStyleSheet(BuildLightThemeStyleSheet());
}

void MainWindow::on_frame_numberSpinBox_valueChanged(int arg1)
{
	// load the previous frame so that overlap can be computed

	if(arg1 > ui->frame_numberSpinBox->minimum())
		Load_Frame_Texture(arg1-1);


	Load_Frame_Texture(arg1);
    ui->playSlider->setValue(  arg1);
     ui->frameNumberTimeCodeLabel->setText( Compute_Timecode_String(arg1));

}


void MainWindow::on_HeightCalculateBtn_clicked()
{
	if (frame_window!=NULL && frame_window->is_calc==false)
	{
		frame_window->is_calc=false;

		frame_window->bestloc=frame_window->bestmatch.postion;

		this->ui->FramePitchendSlider->setValue(
				int( (1.0-(1.0+(frame_window->overlap[3] -
						frame_window->overlap[0]))) * 1000.0));
		NormalizePitchControls(false);

		frame_window->currmatch = frame_window->bestmatch;
		frame_window->currstart = frame_window->overlap[3];
		frame_window->is_calc=true;

		//this->ui->FramePitchendSlider->setEnabled(false);

		this->ui->HeightCalculateBtn->setText("Unlock Height");
	}
	else
	{
		frame_window->is_calc=false;

		this->ui->FramePitchendSlider->setEnabled(true);

		this->ui->HeightCalculateBtn->setText("Lock Height");
	}
}


void MainWindow::on_extractGLButton_clicked()
{
	if(QApplication::keyboardModifiers() & Qt::ShiftModifier)
		extractGL(false);
	else
		extractGL(true);
}

void MainWindow::on_testRunExportButton_clicked()
{
	if(!this->scan.inFile.IsReady()) return;

	const int minFrame = this->scan.inFile.FirstFrame();
	const int maxFrame = this->scan.inFile.LastFrame();
	if(maxFrame < minFrame) return;

	QDialog dialog(this);
	dialog.setWindowTitle("Test Run - framebereik");

	QVBoxLayout layout(&dialog);
	QLabel help(
			"Kies een deelbereik om de instellingen te testen.\n"
			"Een bestaand exportbestand wordt niet overschreven "
			"(oplopend nummer wordt toegevoegd).");
	help.setWordWrap(true);
	layout.addWidget(&help);

	QFormLayout form;
	QSpinBox firstSpin;
	QSpinBox lastSpin;
	firstSpin.setRange(minFrame, maxFrame);
	lastSpin.setRange(minFrame, maxFrame);
	firstSpin.setValue(
			std::max(minFrame, std::min(maxFrame, ui->frameInSpinBox->value())));
	lastSpin.setValue(
			std::max(minFrame, std::min(maxFrame, ui->frameOutSpinBox->value())));

	if(lastSpin.value() < firstSpin.value())
		lastSpin.setValue(firstSpin.value());

	form.addRow("Eerste frame", &firstSpin);
	form.addRow("Laatste frame", &lastSpin);
	layout.addLayout(&form);

	QLabel presetLabel("Snelle presets:");
	layout.addWidget(&presetLabel);

	QHBoxLayout presetLayout;
	QPushButton preset5s("5 sec");
	QPushButton preset100("100");
	QPushButton preset300("300");
	QPushButton preset500("500");
	QPushButton preset10s("10 sec");
	QPushButton preset20s("20 sec");
	QPushButton preset60s("1 min");
	presetLayout.addWidget(&preset5s);
	presetLayout.addWidget(&preset100);
	presetLayout.addWidget(&preset300);
	presetLayout.addWidget(&preset500);
	presetLayout.addWidget(&preset10s);
	presetLayout.addWidget(&preset20s);
	presetLayout.addWidget(&preset60s);
	presetLayout.addStretch(1);
	layout.addLayout(&presetLayout);

	auto currentFps = [&]() -> double
	{
		switch(ui->filmrate_PD->currentIndex())
		{
		case 0: return 23.976;
		case 1: return 24.0;
		case 2: return 25.0;
		default: return 24.0;
		}
	};
	auto applyPresetFrames = [&](int frameCount)
	{
		int startFrame = firstSpin.value();
		int endFrame = startFrame + frameCount - 1;
		endFrame = std::max(startFrame, std::min(maxFrame, endFrame));
		lastSpin.setValue(endFrame);
	};
	connect(&preset5s, &QPushButton::clicked, &dialog,
			[&]()
	{
		int frames = std::max(1, static_cast<int>(std::round(currentFps() * 5.0)));
		applyPresetFrames(frames);
	});
	connect(&preset100, &QPushButton::clicked, &dialog,
			[&]() { applyPresetFrames(100); });
	connect(&preset300, &QPushButton::clicked, &dialog,
			[&]() { applyPresetFrames(300); });
	connect(&preset500, &QPushButton::clicked, &dialog,
			[&]() { applyPresetFrames(500); });
	connect(&preset10s, &QPushButton::clicked, &dialog,
			[&]()
	{
		int tenSecondFrames = std::max(1, static_cast<int>(std::round(currentFps() * 10.0)));
		applyPresetFrames(tenSecondFrames);
	});
	connect(&preset20s, &QPushButton::clicked, &dialog,
			[&]()
	{
		int frames = std::max(1, static_cast<int>(std::round(currentFps() * 20.0)));
		applyPresetFrames(frames);
	});
	connect(&preset60s, &QPushButton::clicked, &dialog,
			[&]()
	{
		int frames = std::max(1, static_cast<int>(std::round(currentFps() * 60.0)));
		applyPresetFrames(frames);
	});

	QCheckBox loopCheck("Loop test-run");
	QSpinBox loopCountSpin;
	loopCountSpin.setRange(2, 20);
	loopCountSpin.setValue(3);
	loopCountSpin.setSuffix("x");
	loopCountSpin.setEnabled(false);

	QHBoxLayout loopLayout;
	loopLayout.addWidget(&loopCheck);
	loopLayout.addWidget(&loopCountSpin);
	loopLayout.addStretch(1);
	layout.addLayout(&loopLayout);

	QCheckBox dryRunCheck(
			"Test zonder wegschrijven (tijdelijke bestanden)");
	layout.addWidget(&dryRunCheck);

	connect(&loopCheck, &QCheckBox::toggled, &dialog,
			[&](bool checked)
	{
		loopCountSpin.setEnabled(checked);
	});
	connect(&firstSpin,
			static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged),
			&dialog,
			[&](int firstValue)
	{
		if(lastSpin.value() < firstValue)
			lastSpin.setValue(firstValue);
	});

	QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout.addWidget(&buttons);
	connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	if(dialog.exec() != QDialog::Accepted)
		return;

	int first = firstSpin.value();
	int last = lastSpin.value();
	if(last < first)
		std::swap(first, last);

	const int prevFrameIn = ui->frameInSpinBox->value();
	const int prevFrameOut = ui->frameOutSpinBox->value();

	ui->frameInSpinBox->setValue(first);
	ui->frameOutSpinBox->setValue(last);

	const int runCount = loopCheck.isChecked() ? loopCountSpin.value() : 1;
	const bool dryRun = dryRunCheck.isChecked();
	const bool suppressDialogs = (runCount > 1) || dryRun;
	const QString prevAudioOutputText = ui->audioOutputFilenameEdit->text();
	const QString prevVideoOutputText = ui->videoOutputFilenameEdit->text();

	int completedRuns = 0;
	bool success = true;
	QString suggestedAudioForLoop = ui->audioOutputFilenameEdit->text().trimmed();
	QStringList keptTempFiles;

	if(runCount > 1 && !dryRun && suggestedAudioForLoop.isEmpty())
	{
		QString expDir = this->prevExportDir;
		if(expDir.isEmpty())
		{
			QSettings settings;
			settings.beginGroup("default-folder");
			expDir = settings.value("export").toString();
			if(expDir.isEmpty())
			{
				expDir = QStandardPaths::writableLocation(
							QStandardPaths::DocumentsLocation);
			}
			settings.endGroup();
		}
		suggestedAudioForLoop = QDir(expDir).filePath(
				BuildOutputNameFromTemplate(".wav"));
	}
	if(runCount > 1 && !dryRun && !suggestedAudioForLoop.isEmpty())
	{
		ui->audioOutputFilenameEdit->setText(suggestedAudioForLoop);
		if(ui->outputVideoCheckBox->isChecked() &&
				ui->videoOutputFilenameEdit->text().trimmed().isEmpty())
		{
			QString expDir = QFileInfo(suggestedAudioForLoop).absolutePath();
			if(expDir.isEmpty()) expDir = QDir::tempPath();
			ui->videoOutputFilenameEdit->setText(QDir(expDir).filePath(
					BuildOutputNameFromTemplate(
							VideoContainerExtensionForMode(videoContainerMode))));
		}
	}

	for(int i = 0; i < runCount; ++i)
	{
		if(dryRun)
		{
			const QString tempStamp = QString("%1-%2")
					.arg(QDateTime::currentMSecsSinceEpoch())
					.arg(i + 1);
			QString tempAudio = QDir::temp().filePath(
					QString("aeo-test-%1.wav").arg(tempStamp));
			QString tempVideo;
			if(ui->outputVideoCheckBox->isChecked())
			{
				tempVideo = QDir::temp().filePath(
						QString("aeo-test-video-%1%2")
								.arg(tempStamp)
								.arg(VideoContainerExtensionForMode(videoContainerMode)));
				ui->videoOutputFilenameEdit->setText(tempVideo);
			}
			ui->audioOutputFilenameEdit->setText(tempAudio);
			success = extractGL(QString(), true, true, true);
			if(autoCleanupTempFilesEnabled)
			{
				QFile::remove(tempAudio);
				if(!tempVideo.isEmpty())
					QFile::remove(tempVideo);
			}
			else
			{
				keptTempFiles << QDir::toNativeSeparators(tempAudio);
				if(!tempVideo.isEmpty())
					keptTempFiles << QDir::toNativeSeparators(tempVideo);
			}
		}
		else
		{
			if(runCount > 1)
				success = extractGL(QString(), true, true, false);
			else
				success = extractGL(true);
		}

		if(!success)
			break;

		++completedRuns;
	}

	ui->frameInSpinBox->setValue(prevFrameIn);
	ui->frameOutSpinBox->setValue(prevFrameOut);
	if(dryRun)
	{
		ui->audioOutputFilenameEdit->setText(prevAudioOutputText);
		ui->videoOutputFilenameEdit->setText(prevVideoOutputText);
		SaveExtractOutputSettings();
	}

	if(suppressDialogs)
	{
		const QString title = success ? tr("Test Run voltooid") :
				tr("Test Run onderbroken");
		const QString mode = dryRun ?
				tr("zonder definitieve outputbestanden") :
				tr("met outputbestanden");
		QString detail = tr("%1 van %2 run(s) uitgevoerd (%3).")
				.arg(completedRuns)
				.arg(runCount)
				.arg(mode);
		if(!keptTempFiles.isEmpty())
		{
			detail += tr("\nTijdelijke bestanden bewaard (%1): %2")
					.arg(keptTempFiles.size())
					.arg(keptTempFiles.join(", "));
		}
		QMessageBox::information(this, title, detail);
	}
}

bool MainWindow::extractGL(QString filename, bool doTimer,
		bool suppressResultDialog, bool skipSidecarWrite)
{
	SaveLastUsedSettingsSnapshot();
	lastExtractFailureReason.clear();
	bool batch = true;
	MetaData meta;
	QString videoFilename;
	outputConflictMode = ClampRangeInt(
			ui->outputConflictModeComboBox->currentIndex(), 0, 1);
	outputOpenFolderAfterExport = ui->outputOpenFolderCheckBox->isChecked();
	autoCleanupTempFilesEnabled = ui->outputAutoCleanupTempCheckBox->isChecked();
	exportReportMode = ClampRangeInt(
			ui->outputExportReportModeComboBox->currentIndex(),
			EXPORT_REPORT_OFF, EXPORT_REPORT_BOTH);
	queueBatchReportMode = ClampRangeInt(
			ui->queueBatchReportModeComboBox->currentIndex(),
			QUEUE_BATCH_REPORT_OFF, QUEUE_BATCH_REPORT_BOTH);
	const bool autoNumberOutputs = (outputConflictMode == 0);
	videoExportMode = ClampRangeInt(
			ui->videoExportModeComboBox->currentIndex(),
			SIMPLE_EXPORT_FAST, SIMPLE_EXPORT_ULTRA);
	videoAudioOffsetCorrectionEnabled = false;
	videoUseStableFrameCrop = ui->videoStableCropCheckBox->isChecked();
	videoDriftSensitivityFactor = std::max(
			0.1f, ui->videoDriftSensitivitySlider->value() / 100.0f);
	videoFlipHorizontal = false;
	videoFlipVertical = false;
	videoRotationDegrees = 0;

	long firstFrame =
			ui->frameInSpinBox->value() - this->scan.inFile.FirstFrame();
	long numFrames =
			ui->frameOutSpinBox->value() - ui->frameInSpinBox->value() + 1;

	int samplingRate = 48000;
	if(ui->filerate_PD->currentIndex() == 1) samplingRate = 96000;
	meta.timeReference = ComputeTimeReference(firstFrame, samplingRate);

	int bitDepth=ui->bitDepthComboBox->currentText().split(" ")[0].toInt();

	QString stereo = (ui->monostereo_PD->currentIndex()==0)?"dual-mono":"stereo";

	meta.codingHistory = QString("A=PCM,F=%1,W=%2,M=%3,T=AEO-Light").
			arg(samplingRate).arg(bitDepth).arg(stereo);

	// Ask for output filename
	if(filename.isEmpty())
	{
		QString expDir;

		if(this->prevExportDir.isEmpty())
		{
			QSettings settings;
			settings.beginGroup("default-folder");
			expDir = settings.value("export").toString();
			if(expDir.isEmpty())
			{
				expDir = QStandardPaths::writableLocation(
							QStandardPaths::DocumentsLocation);
			}
			settings.endGroup();
		}
		else
		{
			expDir = this->prevExportDir;
		}

		filename = ui->audioOutputFilenameEdit->text().trimmed();
		if(filename.isEmpty())
		{
			const QString suggestedAudio = BuildOutputNameFromTemplate(".wav");
			filename = QFileDialog::getSaveFileName(
					this, tr("Export audio to"),
					QDir(expDir).filePath(suggestedAudio), "*.wav");
			if(filename.isEmpty())
			{
				lastExtractFailureReason = tr("Geen audio-outputbestand gekozen.");
				return false;
			}
			ui->audioOutputFilenameEdit->setText(filename);
		}
		this->prevExportDir = QFileInfo(filename).absolutePath();
		WriteLastExportDirMain("last-audio-export", this->prevExportDir);

		const bool outputVideo = ui->outputVideoCheckBox->isChecked();
		if(outputVideo)
		{
			if(this->isVideoMuxingRisky)
			{
				QMessageBox w(QMessageBox::Warning,
						tr("Video Instability Warning"),
						tr("The video creation code is unstable and may crash "
						   "the program when creating a video a second time.\n\n"
						   "Restart AEO-Light to avoid this."),
						QMessageBox::Cancel | QMessageBox::Ignore | QMessageBox::Abort,
						this);
				w.button(QMessageBox::Abort)->setText("Restart");
				w.setDefaultButton(QMessageBox::Cancel);
				int answer = w.exec();
				if(answer == QMessageBox::Abort)
				{
					QTemporaryFile savefile;
					savefile.setAutoRemove(false);
					savefile.open();
					QTextStream stream(&savefile);
					this->saveproject(stream);
					stream << "DeleteMe = true\n";
					savefile.close();
					skipCloseConfirmation = true;
					qApp->quit();
					QStringList args;
					args << savefile.fileName();
					QProcess::startDetached(qApp->arguments()[0], args);
					lastExtractFailureReason = tr("Export afgebroken: herstart aangevraagd.");
					return false;
				}
				else if(answer == QMessageBox::Cancel)
				{
					lastExtractFailureReason = tr("Export geannuleerd door gebruiker.");
					return false;
				}
			}

			videoFilename = ui->videoOutputFilenameEdit->text().trimmed();
			if(videoFilename.isEmpty())
			{
				videoFilename = QDir(expDir).filePath(
						BuildOutputNameFromTemplate(
								VideoContainerExtensionForMode(videoContainerMode)));
				ui->videoOutputFilenameEdit->setText(videoFilename);
			}
			if(videoFilename.isEmpty())
			{
				videoFilename = QFileDialog::getSaveFileName(
						this, tr("Save video as"),
						QDir(expDir).filePath("output" +
								VideoContainerExtensionForMode(videoContainerMode)),
						"*.mp4 *.mov *.mkv");
				if(videoFilename.isEmpty())
				{
					lastExtractFailureReason = tr("Geen video-outputbestand gekozen.");
					return false;
				}
				ui->videoOutputFilenameEdit->setText(videoFilename);
			}
			WriteLastExportDirMain("last-video-export",
					QFileInfo(videoFilename).absolutePath());
			videoAudioOffsetCorrectionEnabled = ui->videoAvOffsetCheckBox->isChecked();
			videoUseStableFrameCrop = ui->videoStableCropCheckBox->isChecked();
			ui->videoStableCropCheckBox->setChecked(videoUseStableFrameCrop);
			UpdateVideoStabilizationControls();
			videoFlipHorizontal = ui->outputFlipHorizontalCheckBox->isChecked();
			videoFlipVertical = ui->outputFlipVerticalCheckBox->isChecked();
			switch(ui->outputRotationComboBox->currentIndex())
			{
			case 1: videoRotationDegrees = 90; break;
			case 2: videoRotationDegrees = 180; break;
			case 3: videoRotationDegrees = 270; break;
			default: videoRotationDegrees = 0; break;
			}
		}
		else
		{
			videoFilename.clear();
			videoAudioOffsetCorrectionEnabled = false;
			videoFlipHorizontal = false;
			videoFlipVertical = false;
			videoRotationDegrees = 0;
		}

		SaveExtractOutputSettings();

		this->currentMeta = &meta;
		batch = false;
	}

	if(autoNumberOutputs)
	{
		filename = EnsureNonOverwritingPath(filename);
		if(!videoFilename.isEmpty())
			videoFilename = EnsureNonOverwritingPath(videoFilename);
	}

	if(!videoFilename.isEmpty())
	{
		QString ffmpegError;
		RunFfmpegSelfTest(&ffmpegError);
		if(!ffmpegError.isEmpty())
		{
			QMessageBox::warning(
					this,
					tr("Video-export niet klaar"),
					tr("FFmpeg-check mislukt:\n%1").arg(ffmpegError));
			lastExtractFailureReason = tr("FFmpeg-check mislukt: %1").arg(ffmpegError);
			return false;
		}
	}

	QString diskSpaceDetails;
	if(!CheckExportDiskSpace(filename, videoFilename, numFrames, &diskSpaceDetails))
	{
		const int answer = QMessageBox::warning(
				this,
				tr("Mogelijk te weinig schijfruimte"),
				tr("De export heeft mogelijk onvoldoende vrije schijfruimte.\n\n%1\n\nToch doorgaan?")
						.arg(diskSpaceDetails),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
		if(answer != QMessageBox::Yes)
		{
			lastExtractFailureReason = tr("Export geannuleerd: onvoldoende schijfruimte.");
			return false;
		}
	}

	QElapsedTimer timer;
	timer.start();

	av_log(NULL, AV_LOG_INFO, "Extract()\n");
	av_log(NULL, AV_LOG_INFO, "WriteAudio: %s\n", filename.toStdString().c_str());
	if(!videoFilename.isEmpty())
		av_log(NULL, AV_LOG_INFO, "WriteVideo: %s\n", videoFilename.toStdString().c_str());
	else
		av_log(NULL, AV_LOG_INFO, "WriteVideo: -no-\n");

	bool success = Extract(filename, videoFilename, firstFrame, numFrames,
			EXTRACT_LOG);
	const double elapsedSec = std::max(0.0, timer.elapsed() / 1000.0);

	QString processMsg;
	QString timingMsg;

	if(success)
	{
		lastExtractFailureReason.clear();
		processMsg = tr("Sound Extraction Complete");
		if(doTimer)
		{
			double fps = numFrames / std::max(0.001, elapsedSec);

			timingMsg = QString(tr("Elapsed time: %1 seconds. (%2 fps)")).
					arg(elapsedSec).arg(fps);
		}
		else
		{
			timingMsg = tr("Sound extraction completed successfully.");
		}

		if(!skipSidecarWrite &&
				ui->xmlSidecarComboBox->currentText() == "Premis")
		{
			write_premis_xml(
					(filename + QString(".xml")).toStdString().c_str(),
					*frame_window, this->scan.inFile,
					filename.toStdString().c_str(),
					firstFrame, numFrames
			);
		}
		else if(!skipSidecarWrite &&
				ui->xmlSidecarComboBox->currentText() == "MODS")
		{
			write_mods_xml(
					(filename + QString(".xml")).toStdString().c_str(),
					*frame_window, this->scan.inFile,
					filename.toStdString().c_str(),
					firstFrame, numFrames
			);
		}

		QString integrityDetails;
		if(!CheckExportIntegrity(filename, videoFilename, numFrames, &integrityDetails))
		{
			processMsg = tr("Extraction Complete (Integrity Warning)");
			if(!integrityDetails.trimmed().isEmpty())
				timingMsg += QString("\n%1").arg(integrityDetails);
			Log() << "Integrity warning: " << integrityDetails << "\n";
		}
		else if(!integrityDetails.trimmed().isEmpty())
		{
			Log() << "Integrity check: " << integrityDetails << "\n";
		}
	}
	else
	{
		bool hasPartialAudio = QFileInfo::exists(filename);
		bool hasPartialVideo = !videoFilename.isEmpty() &&
				QFileInfo::exists(videoFilename);
		if(this->requestCancel && (hasPartialAudio || hasPartialVideo))
		{
			processMsg = tr("Extraction Stopped (Partial Output Saved)");
			timingMsg = tr("Extraction was stopped early. A short preview file was saved for quality review.");
			lastExtractFailureReason = tr("Geannuleerd door gebruiker (gedeeltelijke output opgeslagen).");
		}
		else
		{
			processMsg = tr("Sound Extraction Canceled");
			timingMsg = tr("Sound extraction canceled before finishing.");
			lastExtractFailureReason = tr("Extractie mislukt of geannuleerd.");
		}
	}

	Log() << timingMsg << "\n";
	Log() << QDateTime::currentDateTime().toString() << "\n";
	if(!batch && !skipSidecarWrite)
	{
		const bool canceled = (!success && this->requestCancel);
		WriteSingleExportReport(
				filename,
				videoFilename,
				success,
				canceled,
				firstFrame,
				numFrames,
				elapsedSec,
				timingMsg,
				processMsg);
	}
	if(success && !batch && outputOpenFolderAfterExport && !suppressResultDialog)
	{
		const QString preferredPath =
				!videoFilename.isEmpty() ? videoFilename : filename;
		const QString outDir = QFileInfo(preferredPath).absolutePath();
		if(!outDir.isEmpty())
			QDesktopServices::openUrl(QUrl::fromLocalFile(outDir));
	}

	if(!batch)
	{
		this->LogClose();
		this->currentMeta = NULL;

		if(!suppressResultDialog)
		{
			QMessageBox msg;
			msg.setText(processMsg);
			msg.setInformativeText(timingMsg);
			QPushButton *viewButton =
					msg.addButton("View Log", QMessageBox::HelpRole);
			msg.addButton(QMessageBox::Ok);

			msg.exec();

			if(msg.clickedButton() == viewButton)
			{
				#ifdef Q_OS_WIN32
				system(QString("start %1/%2").arg(QDir::homePath(),"AEO-log.txt")
						.toStdString().c_str());
				#else
				system("open ~/.aeolight.log.txt");
				#endif
			}
		}
	}

	return success;
}

//----------------------------------------------------------------------------
void MainWindow::PlaySample(int index)
{
	ExtractedSound sample;

	if(samplesPlayed.size() <= index) return;

	sample = samplesPlayed[index];

	if(sample.sound == NULL) return;

	if(ui->autoLoadSettingsCheckBox->isChecked())
	{
		ExtractionParametersToGUI(sample);
		GPU_Params_Update(true);
	}

    QSoundEffect *sound = sample.sound;
    if(!sound->isPlaying())
		sound->play();

	return;
}

//----------------------------------------------------------------------------
void MainWindow::on_playSampleButton_clicked()
{
	ExtractedSound sample;

	// create a new sample:
	QTemporaryFile qtmp(QDir::tempPath()+"/aeoXXXXXX.wav");
	// open and close it to ensure the name is fully resolved
	qtmp.setAutoRemove(false);
	qtmp.open();
	qtmp.close();

	long firstFrame = ui->frameInSpinBox->value() -
			this->scan.inFile.FirstFrame();
	long numFrames = 24 * 5;
	if(numFrames >
			this->scan.inFile.LastFrame() -
			ui->frameInSpinBox->value())
	{
		numFrames = this->scan.inFile.LastFrame() -
				ui->frameInSpinBox->value();
	}

	this->currentMeta = NULL;

	sample = Extract(qtmp.fileName(), QString(), firstFrame, numFrames, EXTRACT_LOG);

	static long nSample = 0;

	if(sample.err == 0)
	{
        sample.sound = new QSoundEffect();
        sample.sound->setSource(QUrl::fromLocalFile(qtmp.fileName()));
		sample.sound->play();

		SaveSampleDialog *save = new SaveSampleDialog(this);
		save->setWindowTitle("Save Sample Audio");
		save->exec();

		if(save->result() == QDialog::Accepted)
		{
			switch(save->SelectedSlot())
			{
			case 1:
				if(samplesPlayed[0].sound != NULL)
					delete samplesPlayed[0].sound;

				samplesPlayed[0] = sample;
				ui->playSample1Button->setEnabled(true);
				ui->loadSample1Button->setEnabled(true);
				break;
			case 2:
				if(samplesPlayed[1].sound != NULL)
					delete samplesPlayed[1].sound;

				samplesPlayed[1] = sample;
				ui->playSample2Button->setEnabled(true);
				ui->loadSample2Button->setEnabled(true);
				break;
			case 3:
				if(samplesPlayed[2].sound != NULL)
					delete samplesPlayed[2].sound;

				samplesPlayed[2] = sample;
				ui->playSample3Button->setEnabled(true);
				ui->loadSample3Button->setEnabled(true);
				break;
			case 4:
				if(samplesPlayed[3].sound != NULL)
					delete samplesPlayed[3].sound;

				samplesPlayed[3] = sample;
				ui->playSample4Button->setEnabled(true);
				ui->loadSample4Button->setEnabled(true);
				break;
			}
		}
	}

	return;
}

void MainWindow::on_playSample1Button_clicked()
{
	PlaySample(0);
}

void MainWindow::on_playSample2Button_clicked()
{
	PlaySample(1);
}

void MainWindow::on_playSample3Button_clicked()
{
	PlaySample(2);
}

void MainWindow::on_playSample4Button_clicked()
{
	PlaySample(3);
}

void MainWindow::on_loadSample1Button_clicked()
{
	ExtractionParametersToGUI(samplesPlayed[0]);
	GPU_Params_Update(true);
}

void MainWindow::on_loadSample2Button_clicked()
{
	ExtractionParametersToGUI(samplesPlayed[1]);
	GPU_Params_Update(true);
}

void MainWindow::on_loadSample3Button_clicked()
{
	ExtractionParametersToGUI(samplesPlayed[2]);
	GPU_Params_Update(true);
}

void MainWindow::on_loadSample4Button_clicked()
{
	ExtractionParametersToGUI(samplesPlayed[3]);
	GPU_Params_Update(true);
}

//----------------------------------------------------------------------------

ExtractedSound MainWindow::Extract(QString filename, QString videoFilename,
		long firstFrame, long numFrames, uint8_t flags)
{
	ExtractedSound ret;
	bool success;

	av_log(NULL, AV_LOG_INFO, "Extract() called:\n");
	av_log(NULL, AV_LOG_INFO, "WriteAudio: %s\n", filename.toStdString().c_str());
	if(!videoFilename.isEmpty())
		av_log(NULL, AV_LOG_INFO, "WriteVideo: %s\n", videoFilename.toStdString().c_str());
	else
		av_log(NULL, AV_LOG_INFO, "WriteVideo: -no-\n");

	// pull requested fps from GUI
	switch(ui->filmrate_PD->currentIndex())
	{
	case 0: this->frame_window->fps = 23.976; break;
	case 1: this->frame_window->fps = 24.000; break;
	case 2: this->frame_window->fps = 25.000; break;
	}

	// duration in milliseconds
	this->frame_window->duration = numFrames * this->frame_window->fps * 100.0;

	// pull requested bit depth from GUI
	this->frame_window->bit_depth =
			ui->bitDepthComboBox->currentText().split(" ")[0].toInt();

	switch(ui->filerate_PD->currentIndex())
	{
	case 0: this->frame_window->sampling_rate = 48000; break;
	case 1: this->frame_window->sampling_rate = 96000; break;
	default: this->frame_window->sampling_rate = 0;
	}

	try
	{
		#if 0
		if(this->scan.inFile.IsSynth())
		{
			int ans = QMessageBox::question(this, "Cheat?",
					"Use exact synthetic overlap? (i.e., cheat)",
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No);

			if(ans == QMessageBox::Yes)
				this->scan.cheatOverlap = true;
		}
		#endif

		if(flags & EXTRACT_LOG)
		{
			Log() << QDateTime::currentDateTime().toString() << "\n";
			Log() << Version() << "\n";
			Log() << "Source: " <<
					 QString(scan.inFile.GetFileName().c_str()) << "\n";
			Log() << "Output: " << filename << "\n";

			Log() << "Size: " <<
					 this->scan.inFile.Width() << "x" <<
					 this->scan.inFile.Height() << "\n";

			this->LogSettings();
			this->frame_window->PrintGLVersion(Log());
		}

		if(!this->frame_window->isExposed())
		{
			if(flags & EXTRACT_LOG) Log() << "Frame window not exposed.\n";

			this->frame_window->raise();
			this->frame_window->showNormal();
			this->frame_window->setWindowState(Qt::WindowActive);
			//qApp->setActiveWindow(this->frame_window);

			qApp->processEvents();
		}

		while (!this->frame_window->isExposed())
		{
			if(flags & EXTRACT_LOG)
				Log() << "Frame window still not exposed. "
						"Asking for user help.\n";
			int raise = QMessageBox::question(this, "Select image Window",
					"The image window is obscured. "
					"Please move it to the front.",
					QMessageBox::Ok | QMessageBox::Cancel,
					QMessageBox::Ok);

			if(raise == QMessageBox::Cancel)
			{
				if(flags & EXTRACT_LOG)
					Log() << "User cancelled (could not raise window?)\n";
				ret.err = 1;
				return ret;
			}
		}

		if(flags & EXTRACT_LOG) Log() << "Frame window is exposed.\n";

		void (*prevSegvHandler)(int);
		prevSegvHandler = std::signal(SIGSEGV, SegvHandler);

		traceCurrentOperation = "";
		traceSubroutineOperation = "";
		int jmp = setjmp(segvJumpEnv);
		if(jmp!=0)
		{
			QMessageBox::critical(NULL,"Critical Failure",
					QString("Critical Failure around frame %1.\n%2\n%3").
							arg(lastFrameLoad).arg(traceCurrentOperation).
							arg(traceSubroutineOperation));

			std::exit(1);
		}

		if(flags & EXTRACT_LOG) this->frame_window->logger = &Log();
		this->frame_window->currentOperation = &traceSubroutineOperation;

		success = this->WriteAudioToFile(filename.toStdString().c_str(),
				videoFilename.toStdString().c_str(),
				firstFrame, numFrames);
		this->frame_window->logger = NULL;
		this->frame_window->currentOperation = NULL;

		// restore the previous SEGV handler
		if(prevSegvHandler != SIG_ERR)
			std::signal(SIGSEGV, prevSegvHandler);


	}
	catch(std::exception &e)
	{
		QMessageBox w;
		QString msg("Error extracting sound: \n");
		int answer;

		msg += QString(e.what());

		w.setText(msg);
		w.setStandardButtons(QMessageBox::Abort | QMessageBox::Ok);
		w.setDefaultButton(QMessageBox::Ok);
		answer = w.exec();

		if(answer == QMessageBox::Abort) exit(1);
		ret.err = 2;
		return ret;
	}

	ret = ExtractionParamsFromGUI();

	if(success)
		ret.err = 0;
	else if(this->requestCancel)
		ret.err = 1;
	else
	{
		QMessageBox w;
		QString msg("Error writing wav file.\n");
		int answer;

		w.setText(msg);
		w.setStandardButtons(QMessageBox::Abort | QMessageBox::Ok);
		w.setDefaultButton(QMessageBox::Ok);
		answer = w.exec();

		if(answer == QMessageBox::Abort) exit(1);
		ret.err = 3;
	}

	return ret;
}

#ifdef USE_MUX_HACK
//----------------------------------------------------------------------------
//---------------------- MUX.C -----------------------------------------------
//----------------------------------------------------------------------------
/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat API example.
 *
 * Output a media file in any supported libavformat format. The default
 * codecs are used.
 * @example muxing.c
 */

#define STREAM_DURATION   10.0
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

#define RGB_PIX_FMT       AV_PIX_FMT_RGBA

#define SCALE_FLAGS SWS_BICUBIC

bool MainWindow::EnqueueNextFrame()
{
	if(this->encCurFrame > this->encNumFrames) return false;

	traceCurrentOperation = "Load Texture";

	av_log(NULL, AV_LOG_INFO, "encStartFrame = %ld\n", this->encStartFrame);
	av_log(NULL, AV_LOG_INFO, "encCurFrame = %ld\n", this->encCurFrame);

	long framenum = this->encStartFrame + this->encCurFrame;

	av_log(NULL, AV_LOG_INFO, "framenum = %ld\n", framenum);

	if(framenum > this->scan.inFile.NumFrames()-1)
	{
		// TODO: correct the sound array allocation steps above
		// so that we don't have to load the final frame twice
		av_log(NULL, AV_LOG_INFO, "Load_Frame_Texture(%ld)\n", framenum-1);
		if(!Load_Frame_Texture(framenum - 1)) return false;
	}
	else
	{
		av_log(NULL, AV_LOG_INFO, "Load_Frame_Texture(%ld)\n", framenum);
		if(!Load_Frame_Texture(framenum)) return false;
	}

	// XXX: Warning: this reads to vo.videobuffer instead -- do not change
	// the argument expecting it to work.
	av_log(NULL, AV_LOG_INFO, "this->frame_window->read_frame_texture(this->outputFrameTexture)\n");
	this->frame_window->read_frame_texture(this->outputFrameTexture);

	av_log(NULL, AV_LOG_INFO, "p = new uint8_t[%ld]\n", this->encVideoBufSize);
	uint8_t *p = new uint8_t[this->encVideoBufSize];
	av_log(NULL, AV_LOG_INFO, "memcpy(p, this->frame_window->vo.videobuffer, this->encVideoBufSize)\n");
	memcpy(p, this->frame_window->vo.videobuffer, this->encVideoBufSize);
	av_log(NULL, AV_LOG_INFO, "enqueue p = %p into this->encVideoQueue\n", p);
	this->encVideoQueue.push(p);

	// update audio signal render buffer length
	this->encAudioLen += this->frame_window->samplesperframe_file;
	av_log(NULL, AV_LOG_INFO, "audio render len = %lu\n", (unsigned long)(this->encAudioLen));

	this->encCurFrame++;

	return true;
}

uint8_t *MainWindow::GetVideoFromQueue()
{
	uint8_t *p;

	av_log(NULL, AV_LOG_INFO, "encVideoQueue.empty() = %c\n", encVideoQueue.empty()?'T':'F');
	av_log(NULL, AV_LOG_INFO, "encVideoQueue.size() = %lu\n", encVideoQueue.size());

	if(this->encVideoQueue.empty())
	{
		if(!EnqueueNextFrame()) return NULL;
	}
	p = this->encVideoQueue.front();
	this->encVideoQueue.pop();

	av_log(NULL, AV_LOG_INFO, "return p = %p\n", p);
	return p;
}

AVFrame *MainWindow::GetAudioFromQueue()
{
	const int nbits=16;

	while(this->encS16Frame->pts + this->encS16Frame->nb_samples > this->encAudioLen)
	{
		if(!EnqueueNextFrame()) return NULL;
	}

	int64_t offset = this->encAudioNextPts - this->encAudioPad;
	av_log(NULL, AV_LOG_INFO, "audio render offset = %lld\n", offset);
	av_log(NULL, AV_LOG_INFO, "audio render len = %lld\n", this->encAudioLen);

	if(this->encS16Frame->channels != 2)
		throw AeoException("sound must be stereo");

	int16_t *samples = (int16_t *)(this->encS16Frame->data[0]);
	int s = 0;

	int16_t v;

	if(offset < 0)
	{
		for(int i = 0; i<this->encS16Frame->nb_samples; ++i)
		{
			for(int c = 0; c < this->encS16Frame->channels; ++c)
			{
				samples[s++] = 0;
			}
		}
		av_log(NULL, AV_LOG_INFO, "Silence written nb_samples = %d x%d\n",
				this->encS16Frame->nb_samples, this->encS16Frame->channels);
	}
	else
	{
		// translate the floating point values to S16:
		float **audio = this->frame_window->FileRealBuffer;
		av_log(NULL, AV_LOG_INFO, "audio = FileRealBuffer = [%p,%p]\n",
				audio[0], audio[1]);
		av_log(NULL, AV_LOG_INFO, "Audio copy nb_samples = %d x%d\n",
				this->encS16Frame->nb_samples, this->encS16Frame->channels);
		for(int i = 0; i<this->encS16Frame->nb_samples; ++i)
		{
			for(int c = 0; c < this->encS16Frame->channels; ++c)
			{
				//av_log(NULL, AV_LOG_INFO, "reading audio[%d][%lld]\n", c, offset+i);
				v = int32_t(
						(audio[c][offset+i]*UMAX(nbits))-(UMAX(nbits)/2));
				//av_log(NULL, AV_LOG_INFO, "writing samples[%d]\n", s);
				samples[s++] = v;
			}
		}
		av_log(NULL, AV_LOG_INFO, "Audio copied nb_samples = %d x%d\n",
				this->encS16Frame->nb_samples, this->encS16Frame->channels);
	}

	this->encS16Frame->pts = this->encAudioNextPts;
	this->encAudioNextPts += this->encS16Frame->nb_samples;

	return this->encS16Frame;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
#if 0
	AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

	printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
		   av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
		   av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
		   av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
		   pkt->stream_index);
#endif
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
	/* rescale output packet timestamp values from codec to stream timebase */
	av_packet_rescale_ts(pkt, *time_base, st->time_base);
	pkt->stream_index = st->index;

	/* Write the compressed frame to the media file. */
	log_packet(fmt_ctx, pkt);
	return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
					   AVCodec **codec,
					   enum AVCodecID codec_id)
{
	AVCodecContext *c;
	int i;

	/* find the encoder */
	*codec = avcodec_find_encoder(codec_id);
	if (!(*codec)) {
		fprintf(stderr, "Could not find encoder for '%s'\n",
				avcodec_get_name(codec_id));
		exit(1);
	}

	// allocid: MainWindowStream01
	ost->st = avformat_new_stream(oc, *codec);
	if (!ost->st) {
		fprintf(stderr, "Could not allocate stream\n");
		exit(1);
	}
	av_log(NULL, AV_LOG_INFO,
			"ALLOC new: MainWindowStream01 ost->st->codec = %p\n",
			ost->st->codec);
	ost->st->id = oc->nb_streams-1;
	c = ost->st->codec;

	switch ((*codec)->type) {
	case AVMEDIA_TYPE_AUDIO:
		c->sample_fmt  = (*codec)->sample_fmts ?
			(*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		c->bit_rate    = 64000;

		if(ost->requestedSamplingRate > 0)
			c->sample_rate = ost->requestedSamplingRate;
		else
			c->sample_rate = 44100;

		/* Ugh. Why would you override your setting as soon as you set it?
		 *
		if ((*codec)->supported_samplerates) {
			c->sample_rate = (*codec)->supported_samplerates[0];
			for (i = 0; (*codec)->supported_samplerates[i]; i++) {
				if ((*codec)->supported_samplerates[i] == 44100)
					c->sample_rate = 44100;
			}
		}
		*/

		if ((*codec)->channel_layouts) {
			c->channel_layout = (*codec)->channel_layouts[0];
			for (i = 0; (*codec)->channel_layouts[i]; i++) {
				if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
					c->channel_layout = AV_CH_LAYOUT_STEREO;
			}
		}
		else
			c->channel_layout = AV_CH_LAYOUT_STEREO;

		c->channels = av_get_channel_layout_nb_channels(c->channel_layout);

		ost->st->time_base.num = 1;
		ost->st->time_base.den = c->sample_rate;
		break;

	case AVMEDIA_TYPE_VIDEO:
		c->codec_id = codec_id;

		c->bit_rate = 400000;
		/* Resolution must be a multiple of two. */
		if(ost->requestedWidth > 0)
		{
			c->width = ost->requestedWidth;
			c->height = ost->requestedHeight;
		}
		else
		{
			c->width    = 352;
			c->height   = 288;
		}

		c->bit_rate = c->width * c->height * 4;

		/* timebase: This is the fundamental unit of time (in seconds) in terms
		 * of which frame timestamps are represented. For fixed-fps content,
		 * timebase should be 1/framerate and timestamp increments should be
		 * identical to 1. */
		ost->st->time_base.num = 1;
		ost->st->time_base.den = ost->requestedTimeBase;

		c->time_base       = ost->st->time_base;

		c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
		c->pix_fmt       = STREAM_PIX_FMT;
		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
			/* just for testing, we also add B frames */
			c->max_b_frames = 2;
		}
		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
			/* Needed to avoid using macroblocks in which some coeffs overflow.
			 * This does not happen with normal video, it just happens here as
			 * the motion of the chroma plane does not match the luma plane. */
			c->mb_decision = 2;
		}
	break;

	default:
		break;
	}

#ifndef AV_CODEC_FLAG_GLOBAL_HEADER
#define AV_CODEC_FLAG_GLOBAL_HEADER CODEC_FLAG_GLOBAL_HEADER
#endif

	/* Some formats want stream headers to be separate. */
	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
								  uint64_t channel_layout,
								  int sample_rate, int nb_samples)
{
	AVFrame *frame = av_frame_alloc();
	int ret;

	if (!frame) {
		fprintf(stderr, "Error allocating an audio frame\n");
		exit(1);
	}

	frame->format = sample_fmt;
	frame->channel_layout = channel_layout;
	frame->sample_rate = sample_rate;
	frame->nb_samples = nb_samples;

	if (nb_samples) {
		ret = av_frame_get_buffer(frame, 0);
		if (ret < 0) {
			fprintf(stderr, "Error allocating an audio buffer\n");
			exit(1);
		}
	}

	return frame;
}

static void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
	AVCodecContext *c;
	int nb_samples;
	int ret;
	AVDictionary *opt = NULL;

	c = ost->st->codec;

	/* open it */
	av_dict_copy(&opt, opt_arg, 0);
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		fprintf(stderr, "Could not open audio codec: %s\n", aeo_av_err2str(ret));
		exit(1);
	}

	/* init signal generator */
	ost->t     = 0;
	ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
	/* increment frequency by 110 Hz per second */
	ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

#ifndef AV_CODEC_CAP_VARIABLE_FRAME_SIZE
#define AV_CODEC_CAP_VARIABLE_FRAME_SIZE CODEC_CAP_VARIABLE_FRAME_SIZE
#endif

	if(c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
		nb_samples = 10000;
	else
		nb_samples = c->frame_size;

	// allocid: MainWindowFrame01
	ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
									   c->sample_rate, nb_samples);
	av_log(NULL, AV_LOG_INFO, "ALLOC new: MainWindowFrame01 ost->frame = %p\n",
			ost->frame);
	// allocid: MainWindowFrame02
	ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
									   c->sample_rate, nb_samples);
	av_log(NULL, AV_LOG_INFO,
			"ALLOC new: MainWindowFrame02 ost->tmp_frame = %p\n",
			ost->frame);

	/* create resampler context */
	// allocid: MainWindowSWR01
	ost->swr_ctx = swr_alloc();
	if (!ost->swr_ctx) {
		fprintf(stderr, "Could not allocate resampler context\n");
		exit(1);
	}
	av_log(NULL, AV_LOG_INFO, "ALLOC new: MainWindowSWR01 ost->swr_ctx = %p\n",
			ost->swr_ctx);

	/* set options */
	av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->channels,       0);
	av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
	av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
	av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->channels,       0);
	av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
	av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

	/* initialize the resampling context */
	if ((ret = swr_init(ost->swr_ctx)) < 0) {
		fprintf(stderr, "Failed to initialize the resampling context\n");
		exit(1);
	}
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
AVFrame *MainWindow::get_audio_frame(OutputStream *ost)
{
	AVFrame *frame = ost->tmp_frame;
	int j, i, v;
	int16_t *q = (int16_t*)frame->data[0];
	AVRational avr_one;
	avr_one.num = avr_one.den = 1;

	/* check if we want to generate more frames */
	if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
					  STREAM_DURATION, avr_one) >= 0)
		return NULL;

	av_log(NULL, AV_LOG_INFO, "Audio generate nb_samples = %d x%d\n",
			frame->nb_samples, ost->st->codec->channels);
	for (j = 0; j <frame->nb_samples; j++) {
		v = (int)(sin(ost->t) * 10000);
		for (i = 0; i < ost->st->codec->channels; i++)
			*q++ = v;
		ost->t     += ost->tincr;
		ost->tincr += ost->tincr2;
	}

	frame->pts = ost->next_pts;
	ost->next_pts  += frame->nb_samples;

	return frame;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int MainWindow::write_audio_frame(AVFormatContext *oc, OutputStream *ost)
{
	AVCodecContext *c;
	AVPacket pkt; // data and size must be 0;
	pkt.data = NULL;
	pkt.size = 0;
	AVFrame *frame;
	int ret;
	int got_packet = 0;
	int dst_nb_samples;

	av_init_packet(&pkt);
	c = ost->st->codec;

#if 0
	frame = get_audio_frame(ost);
#else
	av_log(NULL, AV_LOG_INFO, "frame = this->GetAudioFromQueue()\n");
	frame = this->GetAudioFromQueue();
	av_log(NULL, AV_LOG_INFO, "frame = %p\n", frame);
#endif

	if (frame) {
		/* convert samples from native format to destination codec format, using the resampler */
		/* compute destination number of samples */
		av_log(NULL, AV_LOG_INFO, "dst_nb_samples = av_rescale_rnd(...)\n");
		dst_nb_samples = av_rescale_rnd(
				swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
				c->sample_rate, c->sample_rate, AV_ROUND_UP);
		av_assert0(dst_nb_samples == frame->nb_samples);

		/* when we pass a frame to the encoder, it may keep a reference to it
		 * internally;
		 * make sure we do not overwrite it here
		 */
		av_log(NULL, AV_LOG_INFO, "av_frame_make_writable(ost->frame)\n");
		ret = av_frame_make_writable(ost->frame);
		if (ret < 0)
			exit(1);

		/* convert to destination format */
		ret = swr_convert(ost->swr_ctx,
				ost->frame->data, dst_nb_samples,
				(const uint8_t **)frame->data, frame->nb_samples);
		if (ret < 0) {
			fprintf(stderr, "Error while converting\n");
			exit(1);
		}
		frame = ost->frame;

		AVRational r;
		r.num = 1;
		r.den = c->sample_rate;

		frame->pts = av_rescale_q(ost->samples_count, r, c->time_base);
		ost->samples_count += dst_nb_samples;

		ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
		if (ret < 0) {
			fprintf(stderr, "Error encoding audio frame: %s\n", aeo_av_err2str(ret));
			exit(1);
		}

		if (got_packet) {
			ret = write_frame(oc, &c->time_base, ost->st, &pkt);
			if (ret < 0) {
				fprintf(stderr, "Error while writing audio frame: %s\n",
						aeo_av_err2str(ret));
				exit(1);
			}
		}
	}

	return (frame || got_packet) ? 0 : 1;
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture;
	int ret;

	picture = av_frame_alloc();
	if (!picture)
		return NULL;

	picture->format = pix_fmt;
	picture->width  = width;
	picture->height = height;

	/* allocate the buffers for the frame data */
	ret = av_frame_get_buffer(picture, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate frame data.\n");
		exit(1);
	}

	return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
	int ret;
	AVCodecContext *c = ost->st->codec;
	AVDictionary *opt = NULL;

	av_log(NULL, AV_LOG_INFO, "av_dict_copy(&opt, opt_arg, 0)\n");
	av_dict_copy(&opt, opt_arg, 0);

	/* open the codec */
	av_log(NULL, AV_LOG_INFO, "ret = avcodec_open2(c, codec, &opt)\n");
	ret = avcodec_open2(c, codec, &opt);
	av_dict_free(&opt);
	if (ret < 0) {
		fprintf(stderr, "Could not open video codec: %s\n", aeo_av_err2str(ret));
		exit(1);
	}

	/* allocate and init a re-usable frame */
	av_log(NULL, AV_LOG_INFO, "ost->frame = alloc_picture(c->pix_fmt, %d, %d)\n",c->width, c->height);
	// allocid: MainWindowFrame03
	ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!ost->frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	av_log(NULL, AV_LOG_INFO, "ALLOC new: MainWindowFrame03 ost->frame = %p\n",
			ost->frame);

	/* If the output format is not RGBA, then a temporary RGBA
	 * picture is needed too. It is then converted to the required
	 * output format. */
	ost->tmp_frame = NULL;
	if(1 || c->pix_fmt != AV_PIX_FMT_RGBA)
	{
		av_log(NULL, AV_LOG_INFO, "ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, %d, %d)\n",c->width, c->height);
		// allocid: MainWindowFrame04
		ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
		if (!ost->tmp_frame) {
			fprintf(stderr, "Could not allocate temporary picture\n");
			exit(1);
		}
		av_log(NULL, AV_LOG_INFO,
				"ALLOC new: MainWindowFrame04 ost->tmp_frame = %p\n",
				ost->tmp_frame);
	}
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int frame_index,
						   int width, int height)
{
	int x, y, i, ret;

	/* when we pass a frame to the encoder, it may keep a reference to it
	 * internally;
	 * make sure we do not overwrite it here
	 */
	ret = av_frame_make_writable(pict);
	if (ret < 0)
		exit(1);

	i = frame_index;

	/* Y */
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

	/* Cb and Cr */
	for (y = 0; y < height / 2; y++) {
		for (x = 0; x < width / 2; x++) {
			pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
			pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
		}
	}
}

/* Prepare a dummy image. */
static void fill_rgba_image(AVFrame *pict, int frame_index,
						   int width, int height)
{
	int x, y, i, ret;

	/* when we pass a frame to the encoder, it may keep a reference to it
	 * internally;
	 * make sure we do not overwrite it here
	 */
	av_log(NULL, AV_LOG_INFO, "av_frame_make_writable(pict)\n");
	ret = av_frame_make_writable(pict);
	if (ret < 0)
		exit(1);

	i = frame_index;

	av_log(NULL, AV_LOG_INFO, "pict->channels: %d\n", pict->channels);
	av_log(NULL, AV_LOG_INFO, "pict->data[0]: %p\n", pict->data[0]);
	av_log(NULL, AV_LOG_INFO, "pict->data[1]: %p\n", pict->data[1]);
	av_log(NULL, AV_LOG_INFO, "pict->data[2]: %p\n", pict->data[2]);
	av_log(NULL, AV_LOG_INFO, "pict->data[3]: %p\n", pict->data[3]);

	av_log(NULL, AV_LOG_INFO, "fill RGBA\n");
	uint8_t *p;
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			p = pict->data[0] + (y * pict->linesize[0] + x*4);
			p[0] = x + y + i * 3;
			p[1] = 255;
			p[2] = 0;
			p[3] = 255;
		}
	}
}


AVFrame *MainWindow::get_video_frame(OutputStream *ost)
{
	AVCodecContext *c = ost->st->codec;

#if 0
	/* check if we want to generate more frames */
	if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
					  STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
		return NULL;

	if (1 || c->pix_fmt != AV_PIX_FMT_YUV420P) {
		/* as we only generate a YUV420P picture, we must convert it
		 * to the codec pixel format if needed */
		if (!ost->sws_ctx) {
			ost->sws_ctx = sws_getContext(c->width, c->height,
										  AV_PIX_FMT_YUV420P,
										  c->width, c->height,
										  c->pix_fmt,
										  SCALE_FLAGS, NULL, NULL, NULL);
			if (!ost->sws_ctx) {
				fprintf(stderr,
						"Could not initialize the conversion context\n");
				exit(1);
			}
		}
		fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height);
		sws_scale(ost->sws_ctx,
				  (const uint8_t * const *)ost->tmp_frame->data, ost->tmp_frame->linesize,
				  0, c->height, ost->frame->data, ost->frame->linesize);
	} else {
		fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height);
	}
	//#elif 0
	/* check if we want to generate more frames */
	if (av_compare_ts(ost->next_pts, ost->st->codec->time_base,
					  STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
		return NULL;

	if (1 || c->pix_fmt != AV_PIX_FMT_RGBA) {
		/* as we only generate a RGBA picture, we must convert it
		 * to the codec pixel format if needed */
		if (!ost->sws_ctx) {
			av_log(NULL, AV_LOG_INFO, "sws_getContext(...)\n");
			ost->sws_ctx = sws_getContext(c->width, c->height,
										  RGB_PIX_FMT,
										  c->width, c->height,
										  c->pix_fmt,
										  SCALE_FLAGS, NULL, NULL, NULL);
			if (!ost->sws_ctx) {
				fprintf(stderr,
						"Could not initialize the conversion context\n");
				exit(1);
			}
		}
		av_log(NULL, AV_LOG_INFO, "fill_rgba_image(encRGBFrame, ost->next_pts, c->width, c->height);\n");
		fill_rgba_image(encRGBFrame, ost->next_pts, c->width, c->height);
		av_log(NULL, AV_LOG_INFO, "sws_scale(...)\n");
		sws_scale(ost->sws_ctx,
				  (const uint8_t * const *)encRGBFrame->data, encRGBFrame->linesize,
				  0, c->height, ost->frame->data, ost->frame->linesize);
	} else {
		fill_rgba_image(ost->frame, ost->next_pts, c->width, c->height);
	}
	#else
	/* check if we have more frames */
	uint8_t *buf;

	// discard the first few frames of video in order to sync to audio
	while(this->encVideoSkip >0)
	{
		av_log(NULL, AV_LOG_INFO, "videoSkip: buf = GetVideoFromQueue()\n");
		buf = GetVideoFromQueue();
		if(buf==NULL) return NULL;
		delete [] buf;
		this->encVideoSkip--;
	}

	av_log(NULL, AV_LOG_INFO, "buf = GetVideoFromQueue()\n");
	buf = GetVideoFromQueue();
	if(buf==NULL) return NULL;

	if(!ost->sws_ctx)
	{
		av_log(NULL, AV_LOG_INFO, "sws_getContext(...)\n");
		// allocid: MainWindowSWS01
		ost->sws_ctx = sws_getContext(c->width, c->height,
				RGB_PIX_FMT,
				c->width, c->height,
				c->pix_fmt,
				SCALE_FLAGS, NULL, NULL, NULL);
		if(!ost->sws_ctx)
			throw AeoException("Could not initialize sws_ctx");
		av_log(NULL, AV_LOG_INFO,
				"ALLOC new: MainWindowSWS01 ost->sws_ctx = %p\n",
				ost->sws_ctx);
	}

	uint8_t **bufHandle = &buf;

	av_frame_make_writable(ost->frame);
	av_log(NULL, AV_LOG_INFO, "sws_scale(...)\n");
	sws_scale(ost->sws_ctx,
			  (const uint8_t * const *)bufHandle, encRGBFrame->linesize,
			  0, c->height, ost->frame->data, ost->frame->linesize);

	delete [] buf;

	#endif

	ost->frame->pts = ost->next_pts++;

	return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
int MainWindow::write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
	int ret;
	AVCodecContext *c;
	AVFrame *frame;
	int got_packet = 0;
	AVPacket pkt;
	pkt.data = NULL;
	pkt.size = 0;

	c = ost->st->codec;

	av_log(NULL, AV_LOG_INFO, "frame = get_video_frame(ost)\n");
	frame = get_video_frame(ost);

	av_log(NULL, AV_LOG_INFO, "av_init_packet(&pkt)\n");
	av_init_packet(&pkt);

	/* encode the image */
	av_log(NULL, AV_LOG_INFO, "ret = avcodec_encode_video2(c, &pkt, frame, &got_packet)\n");
	ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
	if (ret < 0) {
		fprintf(stderr, "Error encoding video frame: %s\n", aeo_av_err2str(ret));
		exit(1);
	}

	if (got_packet) {
		av_log(NULL, AV_LOG_INFO, "ret = write_frame(oc, &c->time_base, ost->st, &pkt)\n");
		ret = write_frame(oc, &c->time_base, ost->st, &pkt);
	} else {
		ret = 0;
	}

	if (ret < 0) {
		fprintf(stderr, "Error while writing video frame: %s\n", aeo_av_err2str(ret));
		exit(1);
	}

	return (frame || got_packet) ? 0 : 1;
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
	// allocid: MainWindowStream01
	av_log(NULL, AV_LOG_INFO,
			"ALLOC del: MainWindowStream01 ost->st->codec = %p\n",
			ost->st->codec);
	avcodec_close(ost->st->codec);

	// allocid: MainWindowFrame01
	// allocid: MainWindowFrame03
	av_log(NULL, AV_LOG_INFO, "ALLOC del: MainWindowFrame ost->frame = %p\n",
			ost->frame);
	av_frame_free(&ost->frame);

	// allocid: MainWindowFrame02
	// allocid: MainWindowFrame04
	av_log(NULL, AV_LOG_INFO,
			"ALLOC del: MainWindowFrame ost->tmp_frame = %p\n",
			ost->tmp_frame);
	av_frame_free(&ost->tmp_frame);

	// allocid: MainWindowSWS01
	if(ost->sws_ctx)
	{
		av_log(NULL, AV_LOG_INFO, "ALLOC del: MainWindowSWS01 ost->sws_ctx = %p\n",
				ost->sws_ctx);
		sws_freeContext(ost->sws_ctx);
		ost->sws_ctx = NULL;
	}

	// allocid: MainWindowSWR01
	if(ost->swr_ctx)
	{
		av_log(NULL, AV_LOG_INFO, "ALLOC del: MainWindowSWR01 ost->swr_ctx = %p\n",
				ost->swr_ctx);
		swr_free(&ost->swr_ctx);
	}
}

/**************************************************************/
/* media file output */

int MainWindow::MuxMain(const char *fn_arg, long startFrame, long numFrames,
		long vidFrameOffset, QProgressDialog &progress)
{
	int argc = 2;
	const char *argv[] = { "mux_main", fn_arg };
	OutputStream video_st, audio_st;
	memset(&video_st, 0, sizeof(OutputStream));
	memset(&audio_st, 0, sizeof(OutputStream));
	const char *filename = NULL;
	AVOutputFormat *fmt = NULL;
	AVFormatContext *oc = NULL;
	AVCodec *audio_codec = NULL;
	AVCodec *video_codec = NULL;
	int ret;
	int have_video = 0, have_audio = 0;
	int encode_video = 0, encode_audio = 0;
	AVDictionary *opt = NULL;

	// can only do MuxMain once without risking a crash, so mark it now.
	this->isVideoMuxingRisky = true;

	av_log(NULL, AV_LOG_INFO, "MuxMain(\"%s\", %ld, %ld, %ld, progress)\n",
			fn_arg, startFrame, numFrames, vidFrameOffset);


	if (argc < 2) {
		throw AeoException("Invalid call to MuxMain");
	}

	// Call only once (I think libav can handle additional calls all right,
	// but just in case it doesn't...)
	static bool needRegisterAll = true;
	if(needRegisterAll)
	{
		/* Initialize libavcodec, and register all codecs and formats. */
		av_log(NULL, AV_LOG_INFO, "av_register_all()\n");

		av_register_all();
		needRegisterAll = false;
	}

	filename = argv[1];
	/*
	if (argc > 3 && !strcmp(argv[2], "-flags")) {
		av_dict_set(&opt, argv[2]+1, argv[3], 0);
	}
	*/

	/* allocate the output media context */
	av_log(NULL, AV_LOG_INFO, "avformat_alloc_output_context2(&oc, NULL, NULL, filename)\n");
	// allocid: MainWindowOutput01
	avformat_alloc_output_context2(&oc, NULL, NULL, filename);
	if (!oc) {
		printf("Could not deduce output format from file extension: using MPEG.\n");
		avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
	}
	av_log(NULL, AV_LOG_INFO, "ALLOC new: MainWindowOutput01 oc = %p\n", oc);
	if (!oc)
		return 1;

	fmt = oc->oformat;

	/* Add the audio and video streams using the default format codecs
	 * and initialize the codecs. */
	if (fmt->video_codec != AV_CODEC_ID_NONE) {
		int fps_timbase =24;
		if (ui->filmrate_PD->currentIndex()>1) fps_timbase=25;

		video_st.requestedTimeBase = fps_timbase;

		//video_st.requestedWidth = this->scan.inFile.Width();
		//video_st.requestedHeight = this->scan.inFile.Height();
		video_st.requestedWidth = 640;
		video_st.requestedHeight = 480;

		av_log(NULL, AV_LOG_INFO, "add_stream(&video_st, oc, &video_codec, fmt->video_codec)\n");
		add_stream(&video_st, oc, &video_codec, fmt->video_codec);
		have_video = 1;
		encode_video = 1;
	}
	if (fmt->audio_codec != AV_CODEC_ID_NONE) {
		int samplerate = (this->ui->filerate_PD->currentIndex()+1)*48000;

		int frameratesamples ;
		int fps_timbase =24;

		if (ui->filmrate_PD->currentIndex()==0)
			frameratesamples = (int) (samplerate/23.976);
		else if (ui->filmrate_PD->currentIndex()==1)
			frameratesamples = (int) (samplerate/24.0);
		else
		{
			frameratesamples =  (int) (samplerate/25.0);
			fps_timbase=25;
		}

		av_log(NULL, AV_LOG_INFO, "Requested sample rate: %d\n", samplerate);
		audio_st.requestedSamplingRate = samplerate;
		audio_st.requestedSamplesPerFrame = frameratesamples;
		audio_st.requestedTimeBase = fps_timbase;

		av_log(NULL, AV_LOG_INFO, "add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec)\n");
		add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec);
		have_audio = 1;
		encode_audio = 1;
	}

	/* Now that all the parameters are set, we can open the audio and
	 * video codecs and allocate the necessary encode buffers. */
	if (have_video)
		open_video(oc, video_codec, &video_st, opt);

	/*
	av_log(NULL, AV_LOG_INFO, "this->encRGBFrame = alloc_picture(AV_PIX_FMT_RGBA,%d,%d)\n",
			this->scan.inFile.Width(), this->scan.inFile.Height());
	this->encRGBFrame = alloc_picture(RGB_PIX_FMT,
			this->scan.inFile.Width(), this->scan.inFile.Height());
	*/
	this->encRGBFrame = av_frame_alloc();
	if (!this->encRGBFrame) {
		av_log(NULL, AV_LOG_INFO, "Could not allocate RGBFrame picture\n");
		throw AeoException("Could not allocate RGBFrame picture");
	}
	this->encRGBFrame->format = RGB_PIX_FMT;
	this->encRGBFrame->width = 640;
	this->encRGBFrame->height = 480;
	if(av_frame_get_buffer(this->encRGBFrame, 32) < 0)
		throw AeoException("Could not allocate encRGBFrame buffer");

	if (have_audio)
		open_audio(oc, audio_codec, &audio_st, opt);

	av_dump_format(oc, 0, filename, 1);

	av_log(NULL, AV_LOG_INFO, "encS16Frame = audio_st.tmp_frame = %p\n", audio_st.tmp_frame);
	this->encS16Frame = audio_st.tmp_frame;

	/* open the output file, if needed */
	if (!(fmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			fprintf(stderr, "Could not open '%s': %s\n", filename,
					aeo_av_err2str(ret));
			return 1;
		}
	}

	/* Write the stream header, if any. */
	ret = avformat_write_header(oc, &opt);
	if (ret < 0) {
		fprintf(stderr, "Error occurred when opening output file: %s\n",
				aeo_av_err2str(ret));
		return 1;
	}

	this->encCurFrame = 2;
	this->encStartFrame = startFrame;
	this->encNumFrames = numFrames;

	if(vidFrameOffset > 0)
	{
		this->encVideoSkip = vidFrameOffset;
		this->encAudioSkip = 0;
		this->encVideoPad = 0;
		this->encAudioPad = 0;
	}
	else
	{
		this->encVideoSkip = 0;
		this->encAudioSkip = -vidFrameOffset * this->frame_window->samplesperframe_file;
		av_log(NULL, AV_LOG_INFO, "encAudioSkip = %ld\n", this->encAudioSkip);
		this->encVideoPad = 0;
		this->encAudioPad = 0;
	}

	// this had been set to vid.VideoFrameIn->data[0] in the working code
	// via calls to VideoEncoder::ExampleVideoFrame, which set
	// outputFrameTexture->buf to videoFrameIn->data[0], and
	// Frame_Window::PrepareVideoOutput, which set
	// vo.videobuffer to outputFrameTexture->buf

	this->frame_window->vo.videobuffer = this->encRGBFrame->data[0];

	this->encVideoBufSize = 640 * 480 * 4;

	// queue up some black frames to sync the video:
	uint8_t *blackFrame;
	for(int i=0; i<this->encVideoPad; ++i)
	{
		const uint8_t black[4] = { 0, 0, 0, 0xFF };
		blackFrame = new uint8_t[this->encVideoBufSize];
		for(long p = 0; p<this->encVideoBufSize; p+=4)
			memcpy(blackFrame+p, black, 4);
		this->encVideoQueue.push(blackFrame);
	}

	while (encode_video || encode_audio) {
		/* select the stream to encode */
		if (encode_video &&
			(!encode_audio || av_compare_ts(video_st.next_pts, video_st.st->codec->time_base,
											audio_st.next_pts, audio_st.st->codec->time_base) <= 0)) {
			av_log(NULL, AV_LOG_INFO, "encode_video = !write_video_frame(oc, &video_st)\n");
			encode_video = !write_video_frame(oc, &video_st);
		} else {
			av_log(NULL, AV_LOG_INFO, "encode_audio = !write_audio_frame(oc, &audio_st)\n");
			encode_audio = !write_audio_frame(oc, &audio_st);
		}
		progress.setValue(this->encCurFrame);
		if(progress.wasCanceled())
		{
			if(!ConfirmExtractionCancel(
						progress, progress.value(), progress.labelText()))
			{
				continue;
			}
			this->requestCancel = true;
			throw 2;
		}
	}

	/* Write the trailer, if any. The trailer must be written before you
	 * close the CodecContexts open when you wrote the header; otherwise
	 * av_write_trailer() may try to use memory that was freed on
	 * av_codec_close(). */
	av_write_trailer(oc);

	/* Close each codec. */
	if(have_video)
		close_stream(oc, &video_st);

	this->frame_window->vo.videobuffer = NULL;
	av_frame_free(&this->encRGBFrame);

	if(have_audio)
		close_stream(oc, &audio_st);

	if (!(fmt->flags & AVFMT_NOFILE))
		/* Close the output file. */
		avio_closep(&oc->pb);

	/* free the streams */
	// allocid: MainWindowOutput01
	av_log(NULL, AV_LOG_INFO, "ALLOC del: MainWindowOutput01 oc = %p\n", oc);
	avformat_free_context(oc);
	oc = NULL;

	return 0;
}

#undef STREAM_DURATION
#undef STREAM_PIX_FMT
#undef SCALE_FLAGS

//----------------------------------------------------------------------------
//---------------------- END MUX.C -------------------------------------------
//----------------------------------------------------------------------------
#endif

extern "C" void SegvHandler(int param)
{
	longjmp(segvJumpEnv, 1);
}

bool MainWindow::WriteAudioToFile(const char *fn, const char *videoFn,
		long firstFrame, long numFrames)
{
	bool ret;
	long outputNumFrames = numFrames;
	bool stoppedEarlyByUser = false;

	// changing this requires many changes to underlying structures
	const int numChannels = 2;

	ret = true;

	if(videoFn && videoFn[0] == '\0') videoFn = NULL;

	av_log(NULL, AV_LOG_INFO, "WriteAudio: %s\n", fn);
	if(videoFn)
		av_log(NULL, AV_LOG_INFO, "WriteVideo: %s\n", videoFn);
	else
		av_log(NULL, AV_LOG_INFO, "WriteVideo: -no-\n");

	const bool exportingVideo = (videoFn != NULL);
	QProgressDialog progress(
			exportingVideo ? "Extracting Audio/Video..." : "Extracting Audio...",
			"Cancel",0,numFrames);
	progress.setWindowModality(Qt::WindowModal);
	progress.setWindowTitle(
			exportingVideo ? "Extracting Audio/Video..." : "Extracting Audio...");
	progress.setMinimumDuration(0);

	this->requestCancel = false;
	struct AutoOptimizeExtractionGuard
	{
		bool &flag;
		bool previous;
		AutoOptimizeExtractionGuard(bool &ref)
			: flag(ref), previous(ref)
		{
			flag = true;
		}
		~AutoOptimizeExtractionGuard()
		{
			flag = previous;
		}
	} autoOptimizeGuard(suppressFrameAutoOptimizeDuringExtraction);
	QElapsedTimer progressTimer;
	progressTimer.start();
	long progressFramesShown = 0;
	QString progressStatusText = "Audio Rendering...";

	auto updateProgressUi = [&](long completedFrames, const QString &phaseText)
	{
		long shownFrames = std::max<long>(0, std::min<long>(completedFrames, numFrames));
		progressFramesShown = shownFrames;
		progress.setValue(static_cast<int>(shownFrames));

		QString status = QString("%1 (%2/%3 frames)")
				.arg(phaseText)
				.arg(shownFrames)
				.arg(numFrames);
		double elapsedSec = progressTimer.elapsed() / 1000.0;
		status += QString("\nVerstreken: %1")
				.arg(FormatEtaSeconds(elapsedSec));

		if(shownFrames > 0)
		{
			double fps = shownFrames / std::max(0.001, elapsedSec);
			double remainingSec = (numFrames - shownFrames) / std::max(0.001, fps);
			double expectedTotalSec = elapsedSec + remainingSec;
			status += QString(" | Verwacht totaal: %1 | Resterend: %2")
					.arg(FormatEtaSeconds(expectedTotalSec))
					.arg(FormatEtaSeconds(remainingSec));
		}
		else
		{
			status += " | Verwacht totaal: --:-- | Resterend: --:--";
		}

		progressStatusText = status;
		progress.setLabelText(status);
	};

	updateProgressUi(0, "Audio Rendering...");

	//qApp->processEvents();

	frame_window->overrideOverlap = 0;

	int samplerate = (ui->filerate_PD->currentIndex()+1)*48000;

	int frameratesamples ;
	int fps_timbase =24;
#ifndef USELIBAV
	double exportVideoFps = 24.0;
#endif

	if (ui->filmrate_PD->currentIndex()==0)
	{
		frameratesamples = (int) (samplerate/23.976);
		#ifndef USELIBAV
		exportVideoFps = 23.976;
		#endif
	}
	else if (ui->filmrate_PD->currentIndex()==1)
	{
		frameratesamples = (int) (samplerate/24.0);
		#ifndef USELIBAV
		exportVideoFps = 24.0;
		#endif
	}
	else
	{
		frameratesamples =  (int) (samplerate/25.0);
		fps_timbase=25;
		#ifndef USELIBAV
		exportVideoFps = 25.0;
		#endif
	}

	AudioFromTexture audio(numChannels, samplerate, frameratesamples);

	outputFrameTexture= new FrameTexture();
	outputFrameTexture->width=640;
	outputFrameTexture->height=480;

	// See Reference Point frame_view_gl.cpp:LSJ-20170519-1322
	outputFrameTexture->format=GL_UNSIGNED_INT_8_8_8_8_REV;
	outputFrameTexture->bufSize = 640*480*4;
	outputFrameTexture->isNonNativeEndianess = false;
	outputFrameTexture->nComponents = 4;
#ifndef USELIBAV
	if(outputFrameTexture->buf)
	{
		delete [] outputFrameTexture->buf;
		outputFrameTexture->buf = NULL;
	}
	outputFrameTexture->buf = new uint8_t[outputFrameTexture->bufSize];
#endif

	#ifndef USE_MUX_HACK
		#ifdef USELIBAV
		std::unique_ptr<VideoEncoder> vid;
		if(videoFn)
			vid.reset(new VideoEncoder("a.mov"));
		#else
		QTemporaryDir externalVideoFrames;
		externalVideoFrames.setAutoRemove(autoCleanupTempFilesEnabled);
		int externalVideoFrameCount = 0;
		int externalVideoSkipFrames = 0;
		bool overlapCenterBaseSet = false;
		float overlapCenterBase = 0.0f;
		const float uiLeftPixNorm = ui->leftPixSpinBox->value() /
				float(std::max(1u, this->scan.inFile.Width()));
		const float uiRightPixNorm = ui->rightPixSpinBox->value() /
				float(std::max(1u, this->scan.inFile.Width()));
		const float uiVideoTopNorm = ui->videoFrameTopSlider->value() / 1000.0f;
		const float uiVideoBottomNorm = ui->videoFrameBottomSlider->value() / 1000.0f;
		#endif
	#endif

	const int videoAudioSyncAdvanceFrames =
			videoAudioOffsetCorrectionEnabled ? 26 : 0;
	av_log(NULL, AV_LOG_INFO, "Video/audio sync correction = %d frames\n",
			videoAudioSyncAdvanceFrames);
	bool useAutoVideoEq = false;
	double autoVideoBrightness = 0.0;
	double autoVideoContrast = 1.0;
	double autoVideoSaturation = 1.0;
	QString autoVideoSummary;

	if(videoFn)
	{
		#ifndef USE_MUX_HACK
			#ifdef USELIBAV
			// set the incoming size and pixel format
			vid->ExampleVideoFrame(this->outputFrameTexture);
			vid->ExampleAudioFrame(&audio);
			#else
			if(!externalVideoFrames.isValid())
				throw std::runtime_error("Could not create temporary folder for video export");
			externalVideoSkipFrames = videoAudioSyncAdvanceFrames;
			#endif
		#endif

		frame_window->PrepareVideoOutput(this->outputFrameTexture);
		frame_window->is_videooutput = true;
	}

	wav wout(samplerate);
	wout.nChannels = numChannels;

	wout.samplesPerFrame = frameratesamples;
	wout.bitsPerSample = frame_window->bit_depth;

	if(this->currentMeta)
	{
		memset(wout.Originator, 0, 32);
		strncpy(wout.Originator, qPrintable(this->currentMeta->originator), 32);

		memset(wout.OriginatorReference, 0, 32);
		strncpy(wout.OriginatorReference,
				qPrintable(this->currentMeta->originatorReference), 32);

		memset(wout.Description, 0, 256);
		strncpy(wout.Description,
				qPrintable(this->currentMeta->description), 256);

		wout.Version = this->currentMeta->version;

		wout.TimeReferenceHigh = this->currentMeta->timeReference >> 32;
		wout.TimeReferenceLow = this->currentMeta->timeReference;

		memset(wout.CodingHistory, 0, 100);
		strncpy(wout.CodingHistory,
				qPrintable(this->currentMeta->codingHistory), 100);

		// INFO block code is below. It executes after writing data.
	}

	strncpy(wout.OriginationDate,
			qPrintable(QDateTime::currentDateTime().toString("yyyy-MM-dd")),
			10);
	strncpy(wout.OriginationTime,
			qPrintable(QDateTime::currentDateTime().toString("hh:mm:ss")),8);

	frame_window->samplesperframe_file =frameratesamples;
	frame_window->PrepareRecording(numFrames * frameratesamples);

	try
	{
		traceCurrentOperation = "Opening Output File";
		if(wout.open(fn) == NULL) throw 1;

		#ifndef USE_MUX_HACK
		if(videoFn)
		{
			#ifdef USELIBAV
			traceCurrentOperation = "Opening video mux output file";
			vid->Open();
			#endif
		}
		#endif

		traceCurrentOperation = "Load Base Texture";
		if(!Load_Frame_Texture(firstFrame + 0)) throw 1;
		if(videoFn && autoOptimizeVideoEnabled)
		{
			if(ComputeAutoVideoEqSettings(
					autoVideoBrightness,
					autoVideoContrast,
					autoVideoSaturation,
					&autoVideoSummary))
			{
				useAutoVideoEq = true;
				Log() << "Auto video-optimalisatie actief: "
					  << autoVideoSummary << "\n";
			}
		}
		frame_window->is_rendering=true;

		unsigned int sec;
		unsigned int frames;
        QStringList TCL = this->scan.inFile.TimeCode.split(
            QRegularExpression("[:]"),Qt::SkipEmptyParts);
		sec =(((TCL[0].toInt() * 3600 )+ (TCL[1].toInt()* 60)+TCL[2].toInt()));
		frames = TCL[3].toInt() +firstFrame +( sec * fps_timbase);
		frames += ui->advance_CB->currentIndex();
		sec=frames/fps_timbase;
		frames= frames%fps_timbase;
		wout.set_timecode(sec,frames);

		traceCurrentOperation = "Load Texture";
		if(!Load_Frame_Texture(firstFrame + 1)) throw 1;
		outputNumFrames = 1;

		#ifdef USE_MUX_HACK
		if(videoFn)
		{
			traceCurrentOperation = "Mux";
			MuxMain(videoFn, firstFrame, numFrames,
					/* frame skip =  */videoAudioSyncAdvanceFrames,
					progress);
		}
		else
		#endif
		{
			for (long a = 2; a<= numFrames; a++)
			{
				traceCurrentOperation = "Load Texture";\
				if (a + firstFrame > this->scan.inFile.NumFrames()-1 )
				{
					// TODO: correct the sound array allocation steps above
					// so that we don't have to load the final frame twice
					if(!Load_Frame_Texture(firstFrame + a - 1)) break;
				}
				else
				{
					if(!Load_Frame_Texture(firstFrame + a)) break;
				}
				outputNumFrames = a;

				#ifndef USE_MUX_HACK
				if(videoFn)
				{
					frame_window->read_frame_texture(this->outputFrameTexture);
					#ifdef USELIBAV
					vid->WriteVideoFrame(this->outputFrameTexture);
					#else
					if(externalVideoSkipFrames > 0)
					{
						externalVideoSkipFrames--;
					}
					else
					{
						float overlapDriftNorm = 0.0f;
						if(!videoUseStableFrameCrop)
						{
							const float sensitivity =
									std::max(0.1f, videoDriftSensitivityFactor);
							// Use the detected frame-separation line that comes from
							// the overlap search window (Search Size zone). This keeps
							// video crop drift aligned with the same match target used
							// by the overlap detector.
							const float frameSeparationNorm =
									1.0f + (frame_window->overlap[3] -
											frame_window->overlap[0]);
							if(!overlapCenterBaseSet)
							{
								overlapCenterBase = frameSeparationNorm;
								overlapCenterBaseSet = true;
							}
							overlapDriftNorm =
									(frameSeparationNorm - overlapCenterBase) * sensitivity;
							const float driftClamp = std::min(
										0.45f,
										std::max(0.005f,
												frame_window->overlap[1] * sensitivity));
							overlapDriftNorm = std::max(
										-driftClamp,
										std::min(driftClamp, overlapDriftNorm));
						}
						QRect frameCropRect = ComputeAlignedVideoCropRect(
									this->outputFrameTexture, this->frame_window,
									uiLeftPixNorm, uiRightPixNorm,
									uiVideoTopNorm, uiVideoBottomNorm,
									videoUseStableFrameCrop,
									overlapDriftNorm);
						SaveTextureAsPngFrame(
								this->outputFrameTexture,
								externalVideoFrames.path(),
								externalVideoFrameCount++,
								frameCropRect,
								videoFlipHorizontal,
								videoFlipVertical,
								videoRotationDegrees);
					}
					#endif
				}
				#endif

				audio.buf[0] = frame_window->FileRealBuffer[0] +
						(a-2)*frameratesamples;
				if(audio.nChannels == 2)
				{
					audio.buf[1] = frame_window->FileRealBuffer[1] +
							(a-2)*frameratesamples;
				}

				av_log(NULL, AV_LOG_INFO, "audio.buf from FileRealBuffer = [%p,%p]\n",
						frame_window->FileRealBuffer[0],
						frame_window->FileRealBuffer[1]);
				av_log(NULL, AV_LOG_INFO, "offset of %ld*%d yeilds buf = [%p,%p]\n",
						(a-2),frameratesamples, audio.buf[0], audio.buf[1]);

				#ifndef USE_MUX_HACK
				if(videoFn)
				{
					#ifdef USELIBAV
					vid->WriteAudioFrame(&audio);
					#endif
				}
				#endif

				traceCurrentOperation = "Update Progress Bar";

				updateProgressUi(a, videoFn ? "Audio + Video Rendering..." : "Audio Rendering...");
				traceCurrentOperation = "Process GUI events";

				if(progress.wasCanceled())
				{
					if(!ConfirmExtractionCancel(progress,
								static_cast<int>(progressFramesShown), progressStatusText))
					{
						continue;
					}
					this->requestCancel = true;
					stoppedEarlyByUser = true;
					break;
				}

				//if(this->requestCancel) throw 2;
			}
		}

		frame_window->is_rendering =false;

		const int recordedSamples = frame_window->GetRecordedSamples();
		outputNumFrames = recordedSamples / frameratesamples;
		if(outputNumFrames < 1)
			throw 1;

		traceCurrentOperation = "Process Recording";
		frame_window->ProcessRecording(recordedSamples);

		traceCurrentOperation = "Writing wav file";
		wout.writebuffer(frame_window->FileRealBuffer,
				recordedSamples);

		// INFO chunk
		traceCurrentOperation = "Writing Info block";
		updateProgressUi(numFrames, "Finalizing output...");
		wout.BeginInfoChunk();
		wout.AddInfo("ICRD",
				qPrintable(QDate::currentDate().toString("yyyy-MM-dd")));

		if(this->currentMeta)
		{
			wout.AddInfo("IARL",
					qPrintable(this->currentMeta->archivalLocation));
			wout.AddInfo("ICMT",
					qPrintable(this->currentMeta->comment));
			wout.AddInfo("ICOP",
					qPrintable(this->currentMeta->copyright));
		}

		wout.EndInfoChunk();

		traceCurrentOperation = "Closing wav file";
		wout.close();
		traceCurrentOperation = "";

		#ifndef USE_MUX_HACK
		if(videoFn)
		{
			#ifdef USELIBAV
			traceCurrentOperation = "closing video mux output file";
			vid->Close();
			#else
			bool ffmpegCanceled = false;
			if(externalVideoFrameCount <= 0)
				throw std::runtime_error("No video frames were generated for export");
			traceCurrentOperation = "Encoding video file";
			progress.setLabelText("Video coderen... (klik op Cancel om te onderbreken)");
			EncodeVideoWithExternalFfmpeg(
					externalVideoFrames.path(),
					QString::fromLocal8Bit(fn),
					QString::fromLocal8Bit(videoFn),
					exportVideoFps,
					videoExportMode,
					videoContainerMode,
					videoCodecMode,
					videoResolutionMode,
					videoQualityPercent,
					videoAudioBitrateKbps,
					videoPresetMode,
					useAutoVideoEq,
					autoVideoBrightness,
					autoVideoContrast,
					autoVideoSaturation,
					externalVideoFrameCount,
					&progress,
					&ffmpegCanceled,
					!stoppedEarlyByUser);
			if(ffmpegCanceled)
			{
				this->requestCancel = true;
				QFile::remove(QString::fromLocal8Bit(videoFn));
				throw 2;
			}
			if(!autoCleanupTempFilesEnabled)
			{
				Log() << "Temp video frames kept at: "
					  << externalVideoFrames.path() << "\n";
			}
			#endif
		}
		#endif
	}
	catch(int e)
	{
		// silently absorb the exception
		if(e==1) ret = false;
	}
	catch(...)
	{
		throw;
	}

	// clean up:

	frame_window->is_rendering =false;
	frame_window->DestroyRecording();

	if(this->requestCancel)
		ret = false;

	return ret;
}

//*********************IMAGE PROCESSING UI ************************************
QString MainWindow::Compute_Timecode_String(int position)
{
	unsigned int sec;
	unsigned int frames;
	int fps_timebase=24;
	int h,m,s,f;
	if(ui->filmrate_PD->currentIndex()>1)
		fps_timebase=25;

    QStringList TCL =  this->scan.inFile.TimeCode.split(
        QRegularExpression("[:]"),Qt::SkipEmptyParts);
	sec =(((TCL[0].toInt() * 3600 )+ (TCL[1].toInt()* 60)+TCL[2].toInt()));
	frames = TCL[3].toInt() +position;
	sec+=frames/fps_timebase;
	frames= frames%fps_timebase;

	h = (sec/3600);
	sec -= h*3600;
	m = sec/60;
	sec -=m*60;
	s = sec +  (frames/fps_timebase);
	f= frames%fps_timebase;

	//return  return QString::number(h,)+":"+QString::number(m)+":" +QString::number(s)+":"+QString::number(f);

	return  QString("%1").arg(h, 2, 10, QChar('0'))+":"+
			QString("%1").arg(m, 2, 10, QChar('0'))+":"+
			QString("%1").arg(s, 2, 10, QChar('0'))+":"+
			QString("%1").arg(f, 2, 10, QChar('0'));
}

uint64_t MainWindow::ComputeTimeReference(int position, int samplingRate)
{
	unsigned int sec;
	unsigned int frames;
	int fps_timebase=24;
	int h,m,s,f;
	if(ui->filmrate_PD->currentIndex()>1)
		fps_timebase=25;

	uint64_t reference;

    QStringList TCL =  this->scan.inFile.TimeCode.split(
        QRegularExpression("[:]"),Qt::SkipEmptyParts);
	sec =(((TCL[0].toInt() * 3600 )+ (TCL[1].toInt()* 60)+TCL[2].toInt()));
	frames = TCL[3].toInt() +position;

	sec+=frames/fps_timebase;
	frames= frames%fps_timebase;

	reference = sec;
	reference *= samplingRate;
	reference += uint64_t(
			double(samplingRate)*double(frames)/double(fps_timebase));

	return reference;
}

void MainWindow::on_gammaSlider_valueChanged(int value)
{
	ui->gammaLabel->setText(QString::number(value/100.f,'f',2));
	GPU_Params_Update(1);
}

void MainWindow::on_liftSlider_valueChanged(int value)
{
	ui->liftLabel->setText(QString::number(value/100.f,'f',2));
	GPU_Params_Update(1);
}
void MainWindow::on_gainSlider_valueChanged(int value)
{
	ui->gainLabel->setText(QString::number(value/100.f,'f',2));
	GPU_Params_Update(1);
}

void MainWindow::on_thresholdSlider_valueChanged(int value)
{
	ui->threshLabel->setText(QString::number(value/100.f,'f',2));
	GPU_Params_Update(1);
}
void MainWindow::on_blurSlider_valueChanged(int value)
{
	ui->blurLabel->setText(QString::number(value/100.f,'f',2));
	GPU_Params_Update(1);
}

void MainWindow::on_FramePitchendSlider_sliderMoved(int position)
{

}

void MainWindow::on_leftPixSlider_valueChanged(int value)
{
	GPU_Params_Update(1);
}

void MainWindow::on_rightPixSlider_valueChanged(int value)
{
	GPU_Params_Update(1);
}

void MainWindow::on_negBox_clicked()
{
	GPU_Params_Update(1);
}

void MainWindow::on_threshBox_clicked()
{
	GPU_Params_Update(1);
}

void MainWindow::on_FramePitchendSlider_valueChanged(int value)
{
	NormalizePitchControls(false);
	ui->framepitchstartLabel->setText(
			QString::number(ui->framepitchstartSlider->value()/1000.f,'f',3));
	ui->framePitchLabel->setText(
			QString::number(ui->FramePitchendSlider->value()/1000.f,'f',3));
	GPU_Params_Update(1);
}

void MainWindow::on_lift_resetButton_clicked()
{
	ui->liftSlider->setValue(0);
}

void MainWindow::on_gamma_resetButton_clicked()
{
	ui->gammaSlider->setValue(100);
}

void MainWindow::on_gain_resetButton_clicked()
{
	ui->gainSlider->setValue(100);
}

void MainWindow::on_thresh_resetButton_clicked()
{
	ui->thresholdSlider->setValue(300);
}

void MainWindow::on_blur_resetButton_clicked()
{
	ui->blurSlider->setValue(0);
}

void MainWindow::on_desatBox_clicked()
{
	GPU_Params_Update(1);
}

void MainWindow::on_OverlapSoundtrackCheckBox_stateChanged(int arg1)
{
	GPU_Params_Update(1);
}

void MainWindow::on_OverlapPixCheckBox_stateChanged(int arg1)
{
	GPU_Params_Update(1);
}

void MainWindow::on_MainWindow_destroyed()
{
	if(frame_window)
	{
		frame_window->close();
		delete frame_window;
	}
}

void MainWindow::on_CalBtn_clicked()
{
	if(this->frame_window!=NULL)
	{
		this->ui->CalEnableCB->setChecked(false);
		this->frame_window->cal_enabled=false;
		this->frame_window->is_caling=true;

		QProgressDialog progress("Calibrating...", "Cancel", 0, 50);
		progress.setWindowTitle("Calibrating");
		progress.setMinimumDuration(0);
		bool canceled = false;
		progress.setWindowModality(Qt::WindowModal);
		progress.setValue(0);

		this->frame_window->clear_cal=true;
		GPU_Params_Update(1);

		int rnd;
		int floor = 0;
		int ceiling = this->scan.lastFrameIndex-this->scan.firstFrameIndex;
		int range = (ceiling - floor);

		while(this->frame_window->calval<0.5)
		{
			//srand((unsigned)time(0));

			rnd = floor+int(rand()%range);
			Load_Frame_Texture((rnd));

			progress.setValue(100*this->frame_window->calval);
			if(progress.wasCanceled())
			{
				canceled = true;
				break;
			}
		}
		progress.setValue(50); // auto hides the window

		this->frame_window->is_caling=false;
		if(!canceled)
		{
			this->ui->CalEnableCB->setEnabled(true);
			this->ui->CalEnableCB->setChecked(true);
			this->frame_window->cal_enabled=true;
		}
		GPU_Params_Update(1);
	}
}

void MainWindow::on_CalEnableCB_clicked()
{
	if (this->ui->CalEnableCB->isChecked())
	{
		this->frame_window->cal_enabled=true;
	}
	else
	{
		this->frame_window->cal_enabled=false;
	}
	GPU_Params_Update(1);
}

void MainWindow::on_actionAcknowledgements_triggered()
{
	QDesktopServices::openUrl(QUrl("http://imi.cas.sc.edu/mirc/"));
}

void MainWindow::on_actionAbout_triggered()
{
	QMessageBox::information(NULL,
			QString("AEO-Light v")+QString(APP_VERSION_STR),
			"AEO-Light is an open-source software that extracts audio "
			"from optical sound tracks of motion picture film. AEO-Light is "
			"produced at the University of South Carolina by a team comprised "
			"of faculty and staff from the University Libraries' Moving "
			"Image Research Collections (MIRC) and the College of Arts and "
			"Sciences Interdisciplinary Mathematics Institute (IMI), with "
			"contributions from Tommy Aschenbach (Video & Film Solutions). "
			"\n\n"
			"Project funding comes from the Preservation and Access Division"
			"of the National Endowment for the Humanities. AEO-Light is "
			"available through an open-source licensing agreement. The "
			"complete terms are available in the AEO-Light Documentation."
			"\n\n"
			"This software uses libraries from the FFmpeg project under "
			"the GPLv2.0"
			);
}


void MainWindow::DeleteTempSoundFile(void)
{
	try
	{
		ExtractedSound sample;
		ExtractedSound emptySound;
		for(int i=0; i<samplesPlayed.size(); ++i)
		{
			sample = samplesPlayed[i];
			if(sample.sound != NULL)
			{
                sample.sound->stop();
                if(sample.sound->source().isLocalFile())
                    QFile::remove(sample.sound->source().path());
			}
			samplesPlayed[i] = emptySound;
		}
	}
	catch(std::exception &e)
	{
		QMessageBox w;
		QString msg("Error deleting temp file: \n");
		int answer;

		msg += QString(e.what());

		w.setText(msg);
		w.setStandardButtons(QMessageBox::Abort | QMessageBox::Ok);
		w.setDefaultButton(QMessageBox::Ok);
		answer = w.exec();

		if(answer == QMessageBox::Abort) exit(1);
		return;
	}
}

//----------------------------------------------------------------------------
QTextStream &MainWindow::Log()
{
    #ifdef Q_OS_WIN32
	static QFile logfile(QString("%1/%2").arg(QDir::homePath(),
			"AEO-log.txt"));
	#else
	static QFile logfile(QString("%1/%2").arg(QDir::homePath(),
			".aeolight.log.txt"));
	#endif

	if(!log.device() || !log.device()->isOpen())
	{
		logfile.open(QIODevice::WriteOnly);
		log.setDevice(&logfile);
	}

	log.flush();

	return log;
}

void MainWindow::LogSettings()
{
	Log() << "\n----- OpenGL SETTINGS -----\n";
	saveproject(Log());
	Log() << "---------------------------\n";

	if(scan.soundBounds.size() > 0)
	{
		Log() << "\n----- PROJECT SETTINGS -----\n";
		Log() << "FirstFrame: " << scan.firstFrameIndex << "\n";
		Log() << "LastFrame: " << scan.lastFrameIndex << "\n";
		//Log() << "Frames Per Second: " << scan.framesPerSecond << "\n";

		Log() << "Number of soundtracks: " <<
				this->scan.soundBounds.size() << "\n";

		for(int i=0; i<this->scan.soundBounds.size(); ++i)
			Log() << "Sound bounds: " << (scan.soundBounds[i].Left()) <<
					" - " << (scan.soundBounds[i].Right()) << "\n";

		Log() << "----------------------------\n";
	}
}

void MainWindow::on_monostereo_PD_currentIndexChanged(int index)
{
	frame_window->stereo = float(index);
}

void MainWindow::on_actionShow_Soundtrack_Only_triggered()
{
	ui->showSoundtrackOnlyCheckBox->setChecked(
			ui->actionShow_Soundtrack_Only->isChecked());
	GPU_Params_Update(1);
}

void MainWindow::on_actionWaveform_Zoom_triggered()
{
	ui->waveformZoomCheckBox->setChecked(ui->actionWaveform_Zoom->isChecked());
	GPU_Params_Update(1);
}

void MainWindow::on_actionShow_Overlap_triggered()
{
	ui->showOverlapCheckBox->setChecked(ui->actionShow_Overlap->isChecked());
	GPU_Params_Update(1);
}

void MainWindow::on_frameInSpinBox_valueChanged(int arg1)
{
      ui->frameOutTimeCodeLabel->setText(Compute_Timecode_String(arg1 - this->scan.inFile.FirstFrame()));
}

void MainWindow::on_waveformZoomCheckBox_clicked(bool checked)
{
	ui->actionWaveform_Zoom->setChecked(checked);
	GPU_Params_Update(1);
}

void MainWindow::on_showOverlapCheckBox_clicked(bool checked)
{
	ui->actionShow_Overlap->setChecked(checked);
	GPU_Params_Update(1);
}

void MainWindow::on_showSoundtrackOnlyCheckBox_clicked(bool checked)
{
	ui->actionShow_Soundtrack_Only->setChecked(checked);
	GPU_Params_Update(1);
}

void MainWindow::on_actionOpen_Source_triggered()
{
	on_sourceButton_clicked();
}

void MainWindow::on_actionLoad_Settings_triggered()
{
	on_loadprojectButton_clicked();
}

void MainWindow::on_actionSave_Settings_triggered()
{
	on_saveprojectButton_clicked();
}

void MainWindow::on_actionSave_Settings_Preset_triggered()
{
	SaveCurrentSettingsPreset();
}

void MainWindow::on_actionLoad_Settings_Preset_triggered()
{
	LoadSettingsPreset();
}

void MainWindow::on_actionDelete_Settings_Preset_triggered()
{
	DeleteSettingsPreset();
}

void MainWindow::on_actionQuit_triggered()
{
	close();
}

void MainWindow::on_frame_numberSpinBox_editingFinished()
{

}

void MainWindow::on_frameOutSpinBox_valueChanged(int arg1)
{
	ui->frameOutTimeCodeLabel->setText(Compute_Timecode_String(arg1 - this->scan.inFile.FirstFrame()));
}


void MainWindow::on_enqueueButton_clicked()
{
	if(this->extractQueue.size() >= 5) return; // queue full

	// Ask for output filename
	QString expDir;

	if(this->prevExportDir.isEmpty())
	{
		QSettings settings;
		settings.beginGroup("default-folder");
		expDir = settings.value("export").toString();
		if(expDir.isEmpty())
		{
			expDir = QStandardPaths::writableLocation(
						QStandardPaths::DocumentsLocation);
		}
		settings.endGroup();
	}
	else
	{
		expDir = this->prevExportDir;
	}

	QString filename = QFileDialog::getSaveFileName(
				this,tr("Export audio to"),expDir,"*.wav");
	if(filename.isEmpty()) return;

	this->prevExportDir = QFileInfo(filename).absolutePath();

	ExtractTask task;
	task.output = filename;
	task.source = QString(this->scan.inFile.GetFileName().c_str());
	task.srcFormat = this->scan.inFile.GetFormat();

	qDebug() << task.source;

	task.params = ExtractionParamsFromGUI();
	this->extractQueue.push_back(task);

	UpdateQueueWidgets();
}

void MainWindow::UpdateQueueWidgets(void)
{
	size_t i;
	const bool queueIdle = !queueBatchRunning;

	QLabel *label[5];
	label[0] = ui->queue1Label;
	label[1] = ui->queue2Label;
	label[2] = ui->queue3Label;
	label[3] = ui->queue4Label;
	label[4] = ui->queue5Label;

	QPushButton *btn[5];
	btn[0] = ui->queueDelete1Button;
	btn[1] = ui->queueDelete2Button;
	btn[2] = ui->queueDelete3Button;
	btn[3] = ui->queueDelete4Button;
	btn[4] = ui->queueDelete5Button;

    QPushButton *loadBtn[5];
    loadBtn[0] = ui->queueLoad1Button;
    loadBtn[1] = ui->queueLoad2Button;
    loadBtn[2] = ui->queueLoad3Button;
    loadBtn[3] = ui->queueLoad4Button;
    loadBtn[4] = ui->queueLoad5Button;

    QPushButton *updateBtn[5];
    updateBtn[0] = ui->queueUpdate1Button;
    updateBtn[1] = ui->queueUpdate2Button;
    updateBtn[2] = ui->queueUpdate3Button;
    updateBtn[3] = ui->queueUpdate4Button;
    updateBtn[4] = ui->queueUpdate5Button;

	QPushButton *duplicateBtn[5];
	duplicateBtn[0] = ui->queueDuplicate1Button;
	duplicateBtn[1] = ui->queueDuplicate2Button;
	duplicateBtn[2] = ui->queueDuplicate3Button;
	duplicateBtn[3] = ui->queueDuplicate4Button;
	duplicateBtn[4] = ui->queueDuplicate5Button;

	QPushButton *upBtn[5];
	upBtn[0] = ui->queueUp1Button;
	upBtn[1] = ui->queueUp2Button;
	upBtn[2] = ui->queueUp3Button;
	upBtn[3] = ui->queueUp4Button;
	upBtn[4] = ui->queueUp5Button;

	QPushButton *downBtn[5];
	downBtn[0] = ui->queueDown1Button;
	downBtn[1] = ui->queueDown2Button;
	downBtn[2] = ui->queueDown3Button;
	downBtn[3] = ui->queueDown4Button;
	downBtn[4] = ui->queueDown5Button;

    for(i=0; i<this->extractQueue.size(); ++i)
	{
		label[i]->setText(
				QString("%1-%2 %3").
				arg(extractQueue[i].params.frameIn).
				arg(extractQueue[i].params.frameOut).
				arg(QFileInfo(extractQueue[i].source).fileName()));
		label[i]->setEnabled(true);
		btn[i]->setEnabled(queueIdle);
        loadBtn[i]->setEnabled(queueIdle);
        updateBtn[i]->setEnabled(queueIdle);
		duplicateBtn[i]->setEnabled(
				queueIdle && this->extractQueue.size() < 5);
		upBtn[i]->setEnabled(queueIdle && i > 0);
		downBtn[i]->setEnabled(
				queueIdle && i + 1 < this->extractQueue.size());
	}
	ui->queueExtractButton->setEnabled(queueIdle && i > 0);
	for(i=this->extractQueue.size(); i<5; ++i)
	{
		label[i]->setText("Not Set");
		label[i]->setEnabled(false);
		btn[i]->setEnabled(false);
        loadBtn[i]->setEnabled(false);
        updateBtn[i]->setEnabled(false);
		duplicateBtn[i]->setEnabled(false);
		upBtn[i]->setEnabled(false);
		downBtn[i]->setEnabled(false);
    }
	ui->queueStopAfterCurrentCheckBox->setEnabled(
			queueIdle && !this->extractQueue.empty());
	ui->queueRetryFailedButton->setEnabled(
			queueIdle && !failedQueueItems.empty());
	ui->queueImportVFBClipsButton->setEnabled(queueIdle);
	ui->queueSavePresetButton->setEnabled(queueIdle);
	ui->queueLoadPresetButton->setEnabled(queueIdle);
	ui->queueDeletePresetButton->setEnabled(queueIdle);
	ui->queuePauseResumeButton->setEnabled(queueBatchRunning);
	ui->queueSkipCurrentButton->setEnabled(queueBatchRunning);
	ui->queuePauseResumeButton->setText(
			queueBatchPaused ?
					tr("Resume Batch") :
					tr("Pause Batch"));
}


void MainWindow::on_queueDelete1Button_clicked()
{
	if(this->extractQueue.size() < 1) return;
	this->extractQueue.erase(extractQueue.begin());
	UpdateQueueWidgets();
}

void MainWindow::on_queueDelete2Button_clicked()
{
	if(this->extractQueue.size() < 2) return;
	this->extractQueue.erase(extractQueue.begin()+1);
	UpdateQueueWidgets();
}

void MainWindow::on_queueDelete3Button_clicked()
{
	if(this->extractQueue.size() < 3) return;
	this->extractQueue.erase(extractQueue.begin()+2);
	UpdateQueueWidgets();
}

void MainWindow::on_queueDelete4Button_clicked()
{
	if(this->extractQueue.size() < 4) return;
	this->extractQueue.erase(extractQueue.begin()+3);
	UpdateQueueWidgets();
}

void MainWindow::on_queueDelete5Button_clicked()
{
	if(this->extractQueue.size() < 5) return;
	this->extractQueue.erase(extractQueue.begin()+4);
	UpdateQueueWidgets();
}

void MainWindow::QueueLoadSettings(size_t idx)
{
    if(extractQueue.size() <= idx) return;
    if(extractQueue.at(idx).source != QString(scan.inFile.GetFileName().c_str()))
    {
        int ret = QMessageBox::question(
            this, tr("Source Differs"),
            tr("This queue item is for a different source.\n")+
               extractQueue.at(idx).source,
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Ok);
        if(ret == QMessageBox::Cancel) return;
    }
    ExtractionParametersToGUI(extractQueue.at(idx).params);
    ui->frame_numberSpinBox->setValue(extractQueue.at(idx).params.frameIn);
}

void MainWindow::QueueUpdateSettings(size_t idx)
{
    if(extractQueue.size() <= idx) return;
    if(extractQueue.at(idx).source != QString(scan.inFile.GetFileName().c_str()))
    {
        int ret = QMessageBox::question(
            this, tr("Source Differs"),
            tr("This queue item is for a different source.\n")+
                extractQueue.at(idx).source,
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Ok);
        if(ret == QMessageBox::Cancel) return;
    }
    ExtractedSound params = ExtractionParamsFromGUI();
    ExtractTask task = extractQueue.at(idx);
    task.params = params;
    extractQueue.at(idx) = task;
}

void MainWindow::QueueDuplicateSettings(size_t idx)
{
	if(extractQueue.size() <= idx) return;
	if(extractQueue.size() >= 5)
	{
		QMessageBox::information(
				this,
				tr("Queue vol"),
				tr("De queue bevat al 5 items. Verwijder eerst een item om te dupliceren."));
		return;
	}
	extractQueue.insert(extractQueue.begin() + idx + 1, extractQueue.at(idx));
	UpdateQueueWidgets();
}

void MainWindow::QueueMoveSettingsUp(size_t idx)
{
	if(idx == 0 || extractQueue.size() <= idx) return;
	std::swap(extractQueue[idx - 1], extractQueue[idx]);
	UpdateQueueWidgets();
}

void MainWindow::QueueMoveSettingsDown(size_t idx)
{
	if(extractQueue.size() <= idx + 1) return;
	std::swap(extractQueue[idx], extractQueue[idx + 1]);
	UpdateQueueWidgets();
}

void MainWindow::QueueImportVFB()
{
    if(this->extractQueue.size() >= 5) return; // queue full

    int nSlots = 5 - this->extractQueue.size();

    static QString prevDir = "";
    QString srcDir;
    QString expDir;

    if(this->prevExportDir.isEmpty())
    {
        QSettings settings;
        settings.beginGroup("default-folder");
        expDir = settings.value("export").toString();
        if(expDir.isEmpty())
        {
            expDir = QStandardPaths::writableLocation(
                QStandardPaths::DocumentsLocation);
        }
        settings.endGroup();
    }
    else
    {
        expDir = this->prevExportDir;
    }

    if(prevDir.isEmpty())
    {
        QSettings settings;
        settings.beginGroup("default-folder");
        srcDir = settings.value("source").toString();
        if(srcDir.isEmpty())
        {
            QStringList l;
            l = QStandardPaths::standardLocations(
                QStandardPaths::DocumentsLocation);
            if(l.size()) srcDir = l.at(0);
            else srcDir = "/";
        }
        settings.endGroup();
    }
    else
    {
        srcDir = prevDir;
    }

    QString xmlFilename = QFileDialog::getOpenFileName(
        this,
        tr("Virtual Film Bench Exported Clips"),
        srcDir,
        "VFB Export (*.xml)");

    if(xmlFilename.isEmpty()) return;

    prevDir = QFileInfo(xmlFilename).absolutePath();

    QFile xmlFile(xmlFilename);
    if(!xmlFile.open(QFile::ReadOnly | QFile::Text))
    {
        QMessageBox msgError;
        msgError.setText(
            QString("Cannot open file %1").arg(xmlFilename));
        msgError.setIcon(QMessageBox::Critical);
        msgError.setWindowTitle("Error opening XML file");
        msgError.exec();
        return;
    }

    QDomDocument xmlBOM;
    // Set data into the QDomDocument for processing
    QString parseErrorMessage;
    int parseErrorLine(0);
    int parseErrorColumn(0);
    bool parseOk = xmlBOM.setContent(&xmlFile, &parseErrorMessage,
                                     &parseErrorLine, &parseErrorColumn);
    xmlFile.close();

    // nb: the 6.5 docs say casting to bool returns true if there is an
    // error and false otherwise, but the opposite is the case.
    if(!parseOk)
    {
        QMessageBox msgError;
        msgError.setText(
            QString("Error reading XML file, line %1, column %2<br/>%3")
                .arg(parseErrorLine)
                .arg(parseErrorColumn)
                .arg(parseErrorMessage.toHtmlEscaped()));
        msgError.setIcon(QMessageBox::Critical);
        msgError.setWindowTitle("Error reading XML file");
        msgError.exec();
        return;
    }

    QDomElement root = xmlBOM.documentElement();
    QDomElement element;
    QDomElement attr;

    uint32_t start(0);
    uint32_t end(0);
    bool isClip;
    QString tagName;
    QString clipType;

    struct ClipSetting {
        uint32_t start;
        uint32_t end;
        QString label;
    } clip;

    QList<ClipSetting> clipList;

    for(element = root.firstChildElement();
         !element.isNull();
         element = element.nextSiblingElement())
    {
        if(element.tagName() != "event") continue;

        start = end = 0;
        isClip = false;
        clipType = QString();

        // loop over the attributes of this event to find clip params
        for(attr = element.firstChildElement();
             !attr.isNull();
             attr = attr.nextSiblingElement())
        {
            tagName = attr.tagName();

            if(tagName=="location_absolute_in")
            {
                start = attr.firstChild().toText().data().toULong();
            }
            else if(tagName=="location_absolute_out")
            {
                end = attr.firstChild().toText().data().toULong();
            }
            else if(tagName.compare("clip", Qt::CaseInsensitive) == 0)
            {
                isClip = true;
                clipType = attr.firstChild().toText().data();
            }
            else if (tagName=="event_type")
            {
                if(attr.firstChild().toText().data().
                    compare("clip", Qt::CaseInsensitive) == 0)
                {
                    isClip = true;
                }
            }
            else if(tagName.compare("clipType", Qt::CaseInsensitive) == 0)
            {
                clipType = attr.firstChild().toText().data();
            }
        }

        if(isClip)
        {
            clip.start = start;
            clip.end = end;
            clip.label = clipType;
            clipList.append(ClipSetting(clip));
        }
    }

    if(clipList.isEmpty())
    {
        QMessageBox msgError;
        msgError.setText(QString("No 'Clip' event types found"));
        msgError.setIcon(QMessageBox::Information);
        msgError.setWindowTitle("No clips found");
        msgError.exec();
        return;
    }

    ExtractedSound params = ExtractionParamsFromGUI();

    if(clipList.size() > 1 && nSlots > 1)
    {
        QMessageBox msg(this);

        if(clipList.size() <= nSlots)
        {
            msg.setText(QString("%1 clips found.\n"
                        "Do you wish to name the output files "
                        "individually or use a base name plus a count?").
                    arg(clipList.size()));
        }
        else
        {
            msg.setText(QString("%1 clips found, first %2 will be used.\n"
                        "Do you wish to name the output files "
                        "individually or use a base name plus a count?").
                    arg(clipList.size()).arg(nSlots));
            while(clipList.size() > nSlots) clipList.removeLast();
        }

        msg.setWindowTitle("Multiple clips found");
        QPushButton *indButton = new QPushButton("Individually");
        msg.addButton(indButton, QMessageBox::AcceptRole);
        QPushButton *baseButton = new QPushButton("Base with Count");
        msg.addButton(baseButton, QMessageBox::AcceptRole);
        msg.addButton(QMessageBox::Cancel);

        msg.exec();

        if(msg.clickedButton() == baseButton)
        {
            QString filename = QFileDialog::getSaveFileName(
                this,
                tr("Export audio file basename"),
                expDir,"*.wav");
            if(filename.isEmpty()) return;

            QString base = QFileInfo(filename).completeBaseName();
            QString path = QFileInfo(filename).path();
            QString ext = QFileInfo(filename).suffix();

            filename = path + "/" + base + "-%1." + ext;

            for(int i=0; i<clipList.size(); i++)
            {
                clip = clipList.at(i);

                ExtractTask task;
                task.output = filename.arg(i);
                task.source = QString(this->scan.inFile.GetFileName().c_str());
                task.srcFormat = this->scan.inFile.GetFormat();
                task.params = params;
                task.params.frameIn = clip.start;
                task.params.frameOut = clip.end;
                this->extractQueue.push_back(task);
            }
            UpdateQueueWidgets();
            return;
        }
        else if(msg.clickedButton() != indButton) return;

        // individually: fall through to loop below
    }

    // At this point, either:
    // nSlots is 1,
    // clipSize is 1,
    // or user selected to name each output file individually.

    if(nSlots == 1 && clipList.size() > 1)
    {
        QMessageBox::StandardButton btn =
            QMessageBox::information(this, "Multiple Clips Found",
                QString("%1 clips found, first 1 will be used.\n").
                    arg(clipList.size()),
            QMessageBox::Cancel | QMessageBox::Ok,
            QMessageBox::Ok);
        if(btn != QMessageBox::Ok) return;

        while(clipList.size() > 1) clipList.removeLast();
    }

    QList<ExtractTask> taskList;
    for(int i=0; i<clipList.size(); i++)
    {
        clip = clipList.at(i);
        QString filename = QFileDialog::getSaveFileName(
            this,
            tr("Clip (%1 to %2): Export audio to").
                arg(clip.start).arg(clip.end),
            expDir,"*.wav");
        if(filename.isEmpty()) return;

        this->prevExportDir = QFileInfo(filename).absolutePath();

        ExtractTask task;
        task.output = filename;
        task.source = QString(this->scan.inFile.GetFileName().c_str());
        task.srcFormat = this->scan.inFile.GetFormat();
        task.params = ExtractionParamsFromGUI();
        task.params.frameIn = clip.start;
        task.params.frameOut = clip.end;

        taskList.append(task);
    }

    for(auto &task : taskList) this->extractQueue.push_back(task);

    UpdateQueueWidgets();
}

void MainWindow::on_queueExtractButton_clicked()
{
	if(queueBatchRunning)
	{
		QMessageBox::information(
				this,
				tr("Batch bezig"),
				tr("Er is al een batch bezig. Wacht tot die klaar is."));
		return;
	}

	if(this->extractQueue.empty())
	{
		ui->queueBatchStatusLabel->setText(
				tr("Batch status: geen items in de queue."));
		return;
	}

	queueBatchRunning = true;
	queueBatchPaused = false;
	queueBatchSkipRequested = false;
	UpdateQueueWidgets();
	bool retryFailedAfterDialog = false;

	try
	{
	const bool stopAfterCurrent = ui->queueStopAfterCurrentCheckBox->isChecked();
	failedQueueItems.clear();

	const size_t initialTotal = this->extractQueue.size();
	size_t successCount = 0;
	size_t failedCount = 0;
	size_t skippedCount = 0;
	size_t processedCount = 0;
	QStringList batchItemLines;
	QStringList failedDetailLines;
	QStringList skippedDetailLines;
	std::vector<ExtractTask> skippedQueueItems;
	QMap<QString, int> failureReasonCounts;
	QList<double> itemDurationsSec;
	QElapsedTimer batchTimer;
	batchTimer.start();

	while(!this->extractQueue.empty())
	{
		while(queueBatchPaused)
		{
			ui->queueBatchStatusLabel->setText(
					tr("Batch gepauzeerd: %1/%2 verwerkt. Klik Resume Batch om verder te gaan.")
					.arg(processedCount)
					.arg(initialTotal));
			qApp->processEvents();
		}

		QElapsedTimer itemTimer;
		itemTimer.start();
		ExtractTask task = this->extractQueue.front();
		this->extractQueue.erase(this->extractQueue.begin());
		UpdateQueueWidgets();

		const double elapsedSec = std::max(0.0, batchTimer.elapsed() / 1000.0);
		const int progressPct = int(std::round(
				(100.0 * double(processedCount)) /
				std::max<size_t>(1, initialTotal)));
		QString etaText = "--:--";
		const QString currentSourceName = QFileInfo(task.source).fileName();
		const QString currentRangeText = QString("%1-%2")
				.arg(task.params.frameIn)
				.arg(task.params.frameOut);
		if(processedCount > 0)
		{
			const double avgSecPerItem = elapsedSec / double(processedCount);
			const double remainingSec =
					avgSecPerItem * double(initialTotal - processedCount);
			etaText = FormatEtaSeconds(remainingSec);
		}
		ui->queueBatchStatusLabel->setText(
				tr("Batch bezig: item %1/%2 (%3%) - ETA %4 - %5 [%6]")
				.arg(processedCount + 1)
				.arg(initialTotal)
				.arg(progressPct)
				.arg(etaText)
				.arg(currentSourceName)
				.arg(currentRangeText));
		qApp->processEvents();
		Log() << "Batch start item: "
			  << currentSourceName
			  << " ["
			  << currentRangeText
			  << "]\n";

		bool skippedByUser = false;
		if(queueBatchSkipRequested)
		{
			queueBatchSkipRequested = false;
			this->requestCancel = false;
			skippedByUser = true;
		}

		bool sourceReady = false;
		bool success = false;
		QString outcome = tr("Mislukt");
		QString failureReason;
		if(skippedByUser)
		{
			outcome = tr("Overgeslagen");
			failureReason = tr("Handmatig overgeslagen door gebruiker.");
		}
		else
		{
			sourceReady = this->NewSource(task.source, task.srcFormat);
			if(sourceReady)
			{
				this->ExtractionParametersToGUI(task.params);
				success = this->extractGL(task.output);
				if(success)
					outcome = tr("Gelukt");
				else if(queueBatchSkipRequested)
				{
					outcome = tr("Overgeslagen");
					failureReason = tr("Handmatig overgeslagen door gebruiker.");
					skippedByUser = true;
					queueBatchSkipRequested = false;
					this->requestCancel = false;
				}
				else if(this->requestCancel)
					outcome = tr("Geannuleerd");
				if(!success && !skippedByUser)
				{
					failureReason = this->lastExtractFailureReason.trimmed();
					if(failureReason.isEmpty())
					{
						failureReason = this->requestCancel ?
								tr("Geannuleerd door gebruiker.") :
								tr("Onbekende fout tijdens extractie.");
					}
				}
			}
			else
			{
				outcome = tr("Bron niet geladen");
				failureReason = tr("Bron kon niet geladen worden.");
			}
		}
		const double itemElapsedSec = std::max(0.0, itemTimer.elapsed() / 1000.0);
		itemDurationsSec.append(itemElapsedSec);

		++processedCount;
		QString itemLine = QString("%1 | %2 | frames %3-%4 | %5 | %6s")
				.arg(outcome)
				.arg(QFileInfo(task.source).fileName())
				.arg(task.params.frameIn)
				.arg(task.params.frameOut)
				.arg(QDir::toNativeSeparators(task.output))
				.arg(QString::number(itemElapsedSec, 'f', 2));
		if(!failureReason.isEmpty())
			itemLine += QString(" | %1").arg(failureReason);
		batchItemLines << itemLine;
		Log() << "Batch item: " << itemLine << "\n";
		if(success)
		{
			++successCount;
		}
		else if(skippedByUser)
		{
			++skippedCount;
			skippedQueueItems.push_back(task);
			QString skipLine = QString("%1 | frames %2-%3 | %4 | %5s | %6")
					.arg(QFileInfo(task.source).fileName())
					.arg(task.params.frameIn)
					.arg(task.params.frameOut)
					.arg(QDir::toNativeSeparators(task.output))
					.arg(QString::number(itemElapsedSec, 'f', 2))
					.arg(failureReason.trimmed().isEmpty() ?
							tr("Handmatig overgeslagen door gebruiker.") :
							failureReason.trimmed());
			skippedDetailLines << skipLine;
		}
		else
		{
			++failedCount;
			failedQueueItems.push_back(task);
			QString groupedReason = failureReason.trimmed();
			if(groupedReason.isEmpty())
				groupedReason = tr("Onbekende fout tijdens extractie.");
			failureReasonCounts[groupedReason] =
					failureReasonCounts.value(groupedReason, 0) + 1;
			QString failLine = QString("%1 | frames %2-%3 | %4")
					.arg(QFileInfo(task.source).fileName())
					.arg(task.params.frameIn)
					.arg(task.params.frameOut)
					.arg(QDir::toNativeSeparators(task.output));
			failLine += QString(" | %1s").arg(QString::number(itemElapsedSec, 'f', 2));
			failLine += QString(" | %1").arg(groupedReason);
			failedDetailLines << failLine;
			// If the user explicitly canceled, stop the current batch loop.
			if(this->requestCancel)
				break;
		}
		if(stopAfterCurrent)
			break;
	}

	const size_t pendingCount = this->extractQueue.size();
	const double batchElapsedSec = std::max(0.0, batchTimer.elapsed() / 1000.0);
	const double avgSecPerItem =
			batchElapsedSec / std::max<double>(1.0, double(processedCount));
	const double maxSecPerItem = itemDurationsSec.isEmpty() ?
			0.0 :
			*std::max_element(itemDurationsSec.constBegin(), itemDurationsSec.constEnd());
	const QStringList generatedBatchReports = WriteQueueBatchReport(
			initialTotal,
			successCount,
			failedCount,
			skippedCount,
			pendingCount,
			batchElapsedSec,
			failureReasonCounts,
			itemDurationsSec,
			skippedDetailLines,
			batchItemLines);
	const QString reportSuffix = generatedBatchReports.isEmpty() ?
			QString() :
			tr(" Rapport opgeslagen.");
	QList<QPair<QString, int>> sortedFailureReasons;
	for(auto it = failureReasonCounts.constBegin();
			it != failureReasonCounts.constEnd(); ++it)
	{
		sortedFailureReasons.append(qMakePair(it.key(), it.value()));
	}
	std::sort(
			sortedFailureReasons.begin(),
			sortedFailureReasons.end(),
			[](const QPair<QString, int> &a, const QPair<QString, int> &b)
	{
		if(a.second != b.second)
			return a.second > b.second;
		return a.first.toLower() < b.first.toLower();
	});
	QString topFailureSuffix;
	if(failedCount > 0 && !sortedFailureReasons.isEmpty())
	{
		topFailureSuffix = tr(" Top fout: %1 (%2x).")
				.arg(sortedFailureReasons.first().first)
				.arg(sortedFailureReasons.first().second);
	}
	const QString batchSummaryHeader =
			tr("Batch samenvatting: %1 gelukt, %2 gefaald, %3 overgeslagen, %4 wachtend, duur %5, gem. %6s/item.")
					.arg(successCount)
					.arg(failedCount)
					.arg(skippedCount)
					.arg(pendingCount)
					.arg(FormatEtaSeconds(batchElapsedSec))
					.arg(QString::number(avgSecPerItem, 'f', 2));
	ui->queueBatchStatusLabel->setText(
			tr("Batch klaar: %1 gelukt, %2 gefaald, %3 overgeslagen, %4 wachtend, duur %5, gem. %6s/item, max %7s/item.%8%9")
			.arg(successCount)
			.arg(failedCount)
			.arg(skippedCount)
			.arg(pendingCount)
			.arg(FormatEtaSeconds(batchElapsedSec))
			.arg(QString::number(avgSecPerItem, 'f', 2))
			.arg(QString::number(maxSecPerItem, 'f', 2))
			.arg(reportSuffix)
			.arg(topFailureSuffix));
	if(!failedDetailLines.isEmpty() || !skippedDetailLines.isEmpty())
	{
		const QString failureDetailsText = failedDetailLines.join("\n");
		const QString skippedDetailsText = skippedDetailLines.join("\n");
		QStringList reasonLines;
		for(const QPair<QString, int> &entry : sortedFailureReasons)
		{
			reasonLines << QString("%1 (%2x)")
					.arg(entry.first)
					.arg(entry.second);
		}
		const QString reasonSummary = reasonLines.isEmpty() ?
				QString() :
				tr("Fouttypes:\n- %1").arg(reasonLines.join("\n- "));
		const QString batchTimestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
		QString summaryCopyText = tr("Batch samenvatting\nTimestamp: %1\n%2")
				.arg(batchTimestamp)
				.arg(batchSummaryHeader);
		if(!reasonSummary.isEmpty())
			summaryCopyText += QString("\n%1").arg(reasonSummary);
		QString copyText = tr("Batch resultaten (fouten: %1, overgeslagen: %2)\nTimestamp: %3\n%4")
				.arg(failedDetailLines.size())
				.arg(skippedDetailLines.size())
				.arg(batchTimestamp)
				.arg(batchSummaryHeader);
		if(!reasonSummary.isEmpty())
			copyText += QString("\n%1").arg(reasonSummary);
		if(!failureDetailsText.isEmpty())
			copyText += QString("\n\nMislukte items:\n%1").arg(failureDetailsText);
		if(!skippedDetailsText.isEmpty())
			copyText += QString("\n\nOvergeslagen items:\n%1").arg(skippedDetailsText);
		const auto buildBatchDetailsJson = [&]() -> QJsonObject
		{
			QJsonObject root;
			root.insert("timestamp", batchTimestamp);
			root.insert("successCount", static_cast<int>(successCount));
			root.insert("failedCount", static_cast<int>(failedCount));
			root.insert("skippedCount", static_cast<int>(skippedCount));
			root.insert("pendingCount", static_cast<int>(pendingCount));
			root.insert("elapsedSeconds", batchElapsedSec);
			root.insert("avgSecondsPerItem", avgSecPerItem);
			root.insert("summary", batchSummaryHeader);
			if(!reasonSummary.isEmpty())
				root.insert("reasonSummary", reasonSummary);
			QJsonObject reasonCountsObj;
			for(auto it = failureReasonCounts.constBegin();
					it != failureReasonCounts.constEnd(); ++it)
			{
				reasonCountsObj.insert(it.key(), it.value());
			}
			root.insert("failureReasonCounts", reasonCountsObj);
			QJsonArray failedItems;
			for(const QString &line : failedDetailLines)
				failedItems.append(line);
			root.insert("failedItems", failedItems);
			QJsonArray skippedItems;
			for(const QString &line : skippedDetailLines)
				skippedItems.append(line);
			root.insert("skippedItems", skippedItems);
			return root;
		};
		const QByteArray copyJsonBytes =
				QJsonDocument(buildBatchDetailsJson()).toJson(QJsonDocument::Indented);
		const QString copyJsonText = QString::fromUtf8(copyJsonBytes);
		QMessageBox batchWarn(this);
		const bool hasFailures = !failedDetailLines.isEmpty();
		batchWarn.setIcon((this->requestCancel || !hasFailures) ?
				QMessageBox::Information :
				QMessageBox::Warning);
		batchWarn.setWindowTitle(this->requestCancel ?
				tr("Batch onderbroken") :
				(hasFailures ? tr("Batch voltooid met fouten") :
					tr("Batch voltooid met overgeslagen items")));
		if(hasFailures)
		{
			batchWarn.setText(tr("%1 item(s) gefaald, %2 overgeslagen. Open Details voor uitsplitsing.")
					.arg(failedDetailLines.size())
					.arg(skippedDetailLines.size()));
		}
		else
		{
			batchWarn.setText(tr("%1 item(s) overgeslagen. Open Details voor uitsplitsing.")
					.arg(skippedDetailLines.size()));
		}
		QString informative = batchSummaryHeader;
		if(!reasonSummary.isEmpty())
			informative += QString("\n%1").arg(reasonSummary);
		batchWarn.setInformativeText(informative);
		QString detailsText;
		if(!failureDetailsText.isEmpty())
			detailsText += tr("Mislukte items:\n%1").arg(failureDetailsText);
		if(!skippedDetailsText.isEmpty())
		{
			if(!detailsText.isEmpty())
				detailsText += "\n\n";
			detailsText += tr("Overgeslagen items:\n%1").arg(skippedDetailsText);
		}
		batchWarn.setDetailedText(detailsText);
		QPushButton *retryFailedButton = nullptr;
		if(hasFailures)
		{
			const int failedRetryCount = failedDetailLines.size();
			retryFailedButton = batchWarn.addButton(
					tr("Retry gefaalde items (%1)").arg(failedRetryCount),
					QMessageBox::AcceptRole);
		}
		QPushButton *saveDetailsButton = batchWarn.addButton(
				tr("Bewaar details..."),
				QMessageBox::ActionRole);
		QPushButton *copySummaryButton = batchWarn.addButton(
				tr("Kopieer samenvatting"),
				QMessageBox::ActionRole);
		QPushButton *copyDetailsButton = batchWarn.addButton(
				tr("Kopieer details"),
				QMessageBox::ActionRole);
		QPushButton *copyJsonButton = batchWarn.addButton(
				tr("Kopieer JSON"),
				QMessageBox::ActionRole);
		QPushButton *openReportFolderButton = nullptr;
		QPushButton *openReportFileButton = nullptr;
		QPushButton *copyReportPathsButton = nullptr;
		QPushButton *requeueSkippedButton = nullptr;
		QPushButton *requeueAllUnsuccessfulButton = nullptr;
		if(!generatedBatchReports.isEmpty())
		{
			openReportFileButton = batchWarn.addButton(
					tr("Open laatste rapport"),
					QMessageBox::ActionRole);
			openReportFolderButton = batchWarn.addButton(
					tr("Open rapportmap"),
					QMessageBox::ActionRole);
			copyReportPathsButton = batchWarn.addButton(
					tr("Kopieer rapportpaden"),
					QMessageBox::ActionRole);
		}
		if(!skippedQueueItems.empty())
		{
			const int skippedRetryCount = static_cast<int>(skippedQueueItems.size());
			requeueSkippedButton = batchWarn.addButton(
					tr("Zet overgeslagen terug in queue (%1)").arg(skippedRetryCount),
					QMessageBox::ActionRole);
		}
		if(!failedQueueItems.empty() && !skippedQueueItems.empty())
		{
			const int unsuccessfulCount = static_cast<int>(
					failedQueueItems.size() + skippedQueueItems.size());
			requeueAllUnsuccessfulButton = batchWarn.addButton(
					tr("Zet alle niet-gelukte terug in queue (%1)")
							.arg(unsuccessfulCount),
					QMessageBox::ActionRole);
		}
		batchWarn.addButton(QMessageBox::Ok);
		batchWarn.exec();
		if(retryFailedButton && batchWarn.clickedButton() == retryFailedButton)
		{
			retryFailedAfterDialog = true;
		}
		else if(batchWarn.clickedButton() == saveDetailsButton)
		{
			QString baseDir = prevExportDir;
			if(baseDir.isEmpty())
			{
				baseDir = ReadLastExportDirMain(
						"last-audio-export",
						QStandardPaths::writableLocation(
								QStandardPaths::DocumentsLocation));
			}
			const QString suggestName = QString("aeo-batch-resultaten-%1.txt")
					.arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
			QString selectedFilter;
			const QString savePath = QFileDialog::getSaveFileName(
					this,
					tr("Bewaar batch-resultaatdetails"),
					QDir(baseDir).filePath(suggestName),
					tr("Text files (*.txt);;JSON files (*.json)"),
					&selectedFilter);
			if(!savePath.isEmpty())
			{
				bool saveAsJson = selectedFilter.contains(
						"JSON", Qt::CaseInsensitive);
				QString finalSavePath = savePath;
				const QString chosenExt = QFileInfo(finalSavePath).suffix().toLower();
				if(chosenExt == "json")
					saveAsJson = true;
				else if(chosenExt == "txt")
					saveAsJson = false;
				else if(chosenExt.isEmpty())
					finalSavePath += saveAsJson ? ".json" : ".txt";

				QFile outFile(finalSavePath);
				if(outFile.open(QIODevice::WriteOnly | QIODevice::Text))
				{
					if(saveAsJson)
					{
						outFile.write(copyJsonBytes);
					}
					else
					{
						QTextStream out(&outFile);
						out << copyText << "\n";
					}
					outFile.close();
					QMessageBox::information(
							this,
							tr("Details bewaard"),
							tr("Batch-resultaatdetails opgeslagen naar:\n%1")
									.arg(QDir::toNativeSeparators(finalSavePath)));
				}
				else
				{
					QMessageBox::warning(
							this,
							tr("Opslaan mislukt"),
							tr("Kon batch-resultaatdetails niet opslaan naar:\n%1")
									.arg(QDir::toNativeSeparators(finalSavePath)));
				}
			}
		}
		else if(batchWarn.clickedButton() == copyDetailsButton)
		{
			QApplication::clipboard()->setText(
					copyText);
			QMessageBox::information(
					this,
					tr("Details gekopieerd"),
					tr("Batch-resultaatdetails zijn naar het klembord gekopieerd."));
		}
		else if(batchWarn.clickedButton() == copySummaryButton)
		{
			QApplication::clipboard()->setText(summaryCopyText);
			QMessageBox::information(
					this,
					tr("Samenvatting gekopieerd"),
					tr("Batch-samenvatting is naar het klembord gekopieerd."));
		}
		else if(batchWarn.clickedButton() == copyJsonButton)
		{
			QApplication::clipboard()->setText(copyJsonText);
			QMessageBox::information(
					this,
					tr("JSON gekopieerd"),
					tr("Batch-resultaatdetails (JSON) zijn naar het klembord gekopieerd."));
		}
		else if(openReportFolderButton &&
				batchWarn.clickedButton() == openReportFolderButton)
		{
			const QString reportDir =
					QFileInfo(generatedBatchReports.first()).absolutePath();
			if(!QDesktopServices::openUrl(QUrl::fromLocalFile(reportDir)))
			{
				QMessageBox::warning(
						this,
						tr("Map openen mislukt"),
						tr("Kon rapportmap niet openen:\n%1")
								.arg(QDir::toNativeSeparators(reportDir)));
			}
		}
		else if(openReportFileButton &&
				batchWarn.clickedButton() == openReportFileButton)
		{
			const QString reportPath = generatedBatchReports.last();
			if(!QDesktopServices::openUrl(QUrl::fromLocalFile(reportPath)))
			{
				QMessageBox::warning(
						this,
						tr("Rapport openen mislukt"),
						tr("Kon rapport niet openen:\n%1")
								.arg(QDir::toNativeSeparators(reportPath)));
			}
		}
		else if(copyReportPathsButton &&
				batchWarn.clickedButton() == copyReportPathsButton)
		{
			QStringList reportPaths;
			for(const QString &path : generatedBatchReports)
				reportPaths << QDir::toNativeSeparators(path);
			const QString reportPathsText = reportPaths.join("\n");
			QApplication::clipboard()->setText(reportPathsText);
			QMessageBox::information(
					this,
					tr("Rapportpaden gekopieerd"),
					tr("De rapportpaden zijn naar het klembord gekopieerd."));
		}
		else if(requeueSkippedButton &&
				batchWarn.clickedButton() == requeueSkippedButton)
		{
			std::vector<ExtractTask> merged = skippedQueueItems;
			merged.insert(merged.end(), extractQueue.begin(), extractQueue.end());
			const size_t requestedCount = merged.size();
			size_t droppedCount = 0;
			if(merged.size() > 5)
			{
				droppedCount = merged.size() - 5;
				merged.resize(5);
				QMessageBox::information(
						this,
						tr("Queue limiet"),
						tr("Queue toegevoegd: %1 item(s). %2 item(s) niet toegevoegd door queue-limiet.")
								.arg(merged.size())
								.arg(droppedCount));
			}
			extractQueue = merged;
			UpdateQueueWidgets();
			QString requeueStatus = tr("Batch status: %1 overgeslagen item(s) terug in de queue gezet (%2/%3).")
					.arg(skippedQueueItems.size())
					.arg(extractQueue.size())
					.arg(requestedCount);
			if(droppedCount > 0)
			{
				requeueStatus += tr(" %1 item(s) over limiet.").arg(droppedCount);
			}
			ui->queueBatchStatusLabel->setText(requeueStatus);
		}
		else if(requeueAllUnsuccessfulButton &&
				batchWarn.clickedButton() == requeueAllUnsuccessfulButton)
		{
			const size_t failedRequeueCount = failedQueueItems.size();
			const size_t skippedRequeueCount = skippedQueueItems.size();
			std::vector<ExtractTask> merged = failedQueueItems;
			merged.insert(
					merged.end(),
					skippedQueueItems.begin(),
					skippedQueueItems.end());
			merged.insert(merged.end(), extractQueue.begin(), extractQueue.end());
			const size_t requestedCount = merged.size();
			size_t droppedCount = 0;
			if(merged.size() > 5)
			{
				droppedCount = merged.size() - 5;
				merged.resize(5);
				QMessageBox::information(
						this,
						tr("Queue limiet"),
						tr("Queue toegevoegd: %1 item(s). %2 item(s) niet toegevoegd door queue-limiet.")
								.arg(merged.size())
								.arg(droppedCount));
			}
			extractQueue = merged;
			failedQueueItems.clear();
			UpdateQueueWidgets();
			QString requeueAllStatus = tr("Batch status: %1 gefaalde en %2 overgeslagen item(s) terug in de queue gezet (%3/%4).")
					.arg(failedRequeueCount)
					.arg(skippedRequeueCount)
					.arg(extractQueue.size())
					.arg(requestedCount);
			if(droppedCount > 0)
			{
				requeueAllStatus += tr(" %1 item(s) over limiet.").arg(droppedCount);
			}
			ui->queueBatchStatusLabel->setText(requeueAllStatus);
		}
	}
	queueBatchPaused = false;
	queueBatchSkipRequested = false;
	queueBatchRunning = false;
	UpdateQueueWidgets();
	if(retryFailedAfterDialog)
	{
		QTimer::singleShot(0, this, [this]() { this->RetryFailedQueueItems(); });
	}
	}
	catch(const std::exception &e)
	{
		queueBatchPaused = false;
		queueBatchSkipRequested = false;
		queueBatchRunning = false;
		UpdateQueueWidgets();
		ui->queueBatchStatusLabel->setText(
				tr("Batch status: afgebroken door onverwachte fout."));
		QMessageBox::warning(
				this,
				tr("Batch afgebroken"),
				tr("Onverwachte fout tijdens batch:\n%1")
						.arg(QString::fromLocal8Bit(e.what())));
	}
	catch(...)
	{
		queueBatchPaused = false;
		queueBatchSkipRequested = false;
		queueBatchRunning = false;
		UpdateQueueWidgets();
		ui->queueBatchStatusLabel->setText(
				tr("Batch status: afgebroken door onverwachte fout."));
		QMessageBox::warning(
				this,
				tr("Batch afgebroken"),
				tr("Onverwachte fout tijdens batch (onbekende oorzaak)."));
	}
}

void MainWindow::on_soundtrackDefaultsButton_clicked()
{
	SaveDefaultsSoundtrack();
}

void MainWindow::on_soundtrackResetButton_clicked()
{
	if(!scan.inFile.IsReady()) return;

	const int w = scan.inFile.Width();
	ui->OverlapSoundtrackCheckBox->setChecked(true);
	ui->leftSpinBox->setValue(0);
	ui->rightSpinBox->setValue(std::max(1, w - 1));
	ui->monostereo_PD->setCurrentIndex(0);

	ui->OverlapPixCheckBox->setChecked(false);
	ui->leftPixSpinBox->setValue(int(w * 0.475));
	ui->rightPixSpinBox->setValue(int(w * 0.525));
	on_OverlapPixCheckBox_clicked(false);

	ui->framepitchstartSlider->setValue(100);
	ui->FramePitchendSlider->setValue(100);
	NormalizePitchControls(true);
	ui->framepitchstartLabel->setText(
			QString::number(ui->framepitchstartSlider->value()/1000.f,'f',3));
	ui->framePitchLabel->setText(
			QString::number(ui->FramePitchendSlider->value()/1000.f,'f',3));

	ui->videoFrameTopSlider->setValue(100);
	ui->videoFrameBottomSlider->setValue(100);
	NormalizeVideoCropControls(true);
	UpdateVideoCropLabels();
	ui->videoStableCropCheckBox->setChecked(true);
	ui->videoDriftSensitivitySlider->setValue(100);
	UpdateVideoStabilizationControls();

	ui->overlapSlider->setValue(5);
	ui->overlap_label->setText(QString::number(ui->overlapSlider->value()/100.f,'f',2));
	ui->HeightCalculateBtn->setChecked(false);
	ui->HeightCalculateBtn->setText("Lock Height");

	GPU_Params_Update(1);
}

void MainWindow::on_imageDefaultsButton_clicked()
{
	SaveDefaultsImage();
}

void MainWindow::on_imageResetButton_clicked()
{
	ui->liftSlider->setValue(0);
	ui->gammaSlider->setValue(100);
	ui->gainSlider->setValue(100);
	ui->thresholdSlider->setValue(300);
	ui->blurSlider->setValue(0);
	ui->threshBox->setChecked(false);
	ui->negBox->setChecked(false);
	ui->desatBox->setChecked(false);
	ui->liftLabel->setText(QString::number(0.0f,'f',2));
	ui->gammaLabel->setText(QString::number(1.0f,'f',2));
	ui->gainLabel->setText(QString::number(1.0f,'f',2));
	ui->threshLabel->setText(QString::number(3.0f,'f',2));
	ui->blurLabel->setText(QString::number(0.0f,'f',2));
	GPU_Params_Update(1);
}

void MainWindow::on_extractDefaultsButton_clicked()
{
	SaveDefaultsAudio();
}

void MainWindow::on_extractResetButton_clicked()
{
	if(scan.inFile.IsReady())
	{
		ui->frameInSpinBox->setValue(scan.inFile.FirstFrame());
		ui->frameOutSpinBox->setValue(scan.inFile.LastFrame());
	}
	ui->filerate_PD->setCurrentIndex(0);
	ui->bitDepthComboBox->setCurrentIndex(0);
	const int idx16mm = ui->advance_CB->findText("26 - 16mm Standard",
			Qt::MatchFixedString);
	ui->advance_CB->setCurrentIndex(idx16mm >= 0 ? idx16mm : 0);
	ui->audioQualityPresetComboBox->setCurrentIndex(AUDIO_QUALITY_PRESET_COMPACT);
	ui->audioOnlyQualityPresetComboBox->setCurrentIndex(AUDIO_QUALITY_PRESET_COMPACT);
	ui->outputVideoCheckBox->setChecked(false);
	ui->videoAvOffsetCheckBox->setChecked(true);
	ui->outputFlipHorizontalCheckBox->setChecked(false);
	ui->outputFlipVerticalCheckBox->setChecked(false);
	ui->outputRotationComboBox->setCurrentIndex(0);
	ui->outputVideoSizeComboBox->setCurrentIndex(VIDEO_RESOLUTION_SOURCE);
	ui->outputFilenameTemplateEdit->setText("{source}_{datetime}");
	ui->outputConflictModeComboBox->setCurrentIndex(0);
	ui->outputOpenFolderCheckBox->setChecked(false);
	ui->outputAutoCleanupTempCheckBox->setChecked(true);
	ui->outputExportReportModeComboBox->setCurrentIndex(EXPORT_REPORT_OFF);
	ui->queueBatchReportModeComboBox->setCurrentIndex(QUEUE_BATCH_REPORT_OFF);
	videoResolutionMode = VIDEO_RESOLUTION_SOURCE;
	SaveExtractOutputSettings();
	SaveVideoExportSettings();
	UpdateExtractOutputControls();
	UpdateVideoExportControls();
}

void MainWindow::on_actionPreferences_triggered()
{
	preferencesdialog *pref = new preferencesdialog(this);
	pref->setWindowTitle("Preferences");
	const int result = pref->exec();

	// the dialog itself modifies the app's preferences if accepted,
	// so we only need to re-apply live UI preferences after save.
	if(result == QDialog::Accepted)
	{
		ApplyThemeFromSettings();
		ApplyCompactModePreference();
		UpdateAdaptiveUiScale();
		PositionPreviewWindowBesideMainWindow();
	}

	delete pref;
}

void MainWindow::on_actionReport_or_track_an_issue_triggered()
{
	QDesktopServices::openUrl(QUrl("https://github.com/usc-imi/aeo-light/issues"));
}

void MainWindow::on_OverlapPixCheckBox_clicked(bool checked)
{
	ui->pixLabel->setEnabled(checked);
	ui->leftPixSpinBox->setEnabled(checked);
	ui->rightPixSpinBox->setEnabled(checked);
	ui->leftPixSlider->setEnabled(checked);
	ui->rightPixSlider->setEnabled(checked);
}

void MainWindow::on_degreeSpinBox_valueChanged(double arg1)
{

    GPU_Params_Update(1);
}


void MainWindow::on_rotateCheckbox_stateChanged(int arg1)
{
        GPU_Params_Update(1);
}

void MainWindow::on_audioOutputBrowseButton_clicked()
{
	QString startDir = prevExportDir;
	if(startDir.isEmpty())
	{
		startDir = ReadLastExportDirMain("last-audio-export",
				QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	}
	QString filename = QFileDialog::getSaveFileName(
			this, tr("Export audio to"),
			QDir(startDir).filePath(ui->audioOutputFilenameEdit->text()),
			"*.wav");
	if(filename.isEmpty()) return;

	ui->audioOutputFilenameEdit->setText(filename);
	prevExportDir = QFileInfo(filename).absolutePath();
	WriteLastExportDirMain("last-audio-export", prevExportDir);

	if(ui->videoOutputFilenameEdit->text().trimmed().isEmpty())
	{
		QFileInfo fi(filename);
		ui->videoOutputFilenameEdit->setText(
				fi.path() + "/" + fi.completeBaseName() +
				VideoContainerExtensionForMode(videoContainerMode));
	}
	UpdateVideoExportControls();
	SaveExtractOutputSettings();
}

void MainWindow::on_videoOutputBrowseButton_clicked()
{
	QString fallback = prevExportDir;
	if(fallback.isEmpty())
		fallback = ReadLastExportDirMain("last-video-export",
				QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
	QString filename = QFileDialog::getSaveFileName(
			this, tr("Save video as"),
			QDir(fallback).filePath(ui->videoOutputFilenameEdit->text()),
			"*.mp4 *.mov *.mkv");
	if(filename.isEmpty()) return;

	ui->videoOutputFilenameEdit->setText(filename);
	QString suffix = QFileInfo(filename).suffix().toLower();
	if(suffix == "mov")
		videoContainerMode = VIDEO_CONTAINER_MOV;
	else if(suffix == "mkv")
		videoContainerMode = VIDEO_CONTAINER_MKV;
	else
		videoContainerMode = VIDEO_CONTAINER_MP4;
	UpdateVideoExportControls();
	SaveVideoExportSettings();
	WriteLastExportDirMain("last-video-export", QFileInfo(filename).absolutePath());
	SaveExtractOutputSettings();
}

void MainWindow::on_videoOutputFilenameEdit_editingFinished()
{
	UpdateVideoExportControls();
	SaveExtractOutputSettings();
}

void MainWindow::on_outputVideoSizeComboBox_currentIndexChanged(int index)
{
	videoResolutionMode = ClampRangeInt(
			index, VIDEO_RESOLUTION_SOURCE, VIDEO_RESOLUTION_3840X2160);
	SaveVideoExportSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_outputFilenameTemplateEdit_editingFinished()
{
	SaveExtractOutputSettings();
}

void MainWindow::on_outputConflictModeComboBox_currentIndexChanged(int index)
{
	outputConflictMode = ClampRangeInt(index, 0, 1);
	SaveExtractOutputSettings();
}

void MainWindow::on_outputOpenFolderCheckBox_toggled(bool checked)
{
	outputOpenFolderAfterExport = checked;
	SaveExtractOutputSettings();
}

void MainWindow::on_outputAutoCleanupTempCheckBox_toggled(bool checked)
{
	autoCleanupTempFilesEnabled = checked;
	SaveExtractOutputSettings();
}

void MainWindow::on_outputExportReportModeComboBox_currentIndexChanged(int index)
{
	exportReportMode = ClampRangeInt(index, EXPORT_REPORT_OFF, EXPORT_REPORT_BOTH);
	SaveExtractOutputSettings();
}

void MainWindow::on_outputVideoCheckBox_toggled(bool checked)
{
	if(checked && ui->videoOutputFilenameEdit->text().trimmed().isEmpty())
	{
		QString audioPath = ui->audioOutputFilenameEdit->text().trimmed();
		if(!audioPath.isEmpty())
		{
			QFileInfo fi(audioPath);
			ui->videoOutputFilenameEdit->setText(
					fi.path() + "/" + fi.completeBaseName() +
					VideoContainerExtensionForMode(videoContainerMode));
		}
	}
	UpdateExtractOutputControls();
	UpdateVideoExportControls();
	SaveExtractOutputSettings();
}

void MainWindow::on_outputVideoOrientationPresetButton_clicked()
{
	ui->outputFlipHorizontalCheckBox->setChecked(true);
	ui->outputFlipVerticalCheckBox->setChecked(false);
	ui->outputRotationComboBox->setCurrentIndex(0);
	SaveExtractOutputSettings();
}

void MainWindow::on_outputFlipHorizontalCheckBox_toggled(bool checked)
{
	Q_UNUSED(checked);
	SaveExtractOutputSettings();
}

void MainWindow::on_outputFlipVerticalCheckBox_toggled(bool checked)
{
	Q_UNUSED(checked);
	SaveExtractOutputSettings();
}

void MainWindow::on_audioQualityPresetComboBox_currentIndexChanged(int index)
{
	if(audioQualityPresetSyncLock) return;

	audioQualityPresetSyncLock = true;
	switch(index)
	{
	case AUDIO_QUALITY_PRESET_COMPACT:
		ui->filerate_PD->setCurrentIndex(0); // 48khz
		ui->bitDepthComboBox->setCurrentIndex(0); // 16 bit
		ui->videoExportAudioBitrateSpinBox->setValue(128);
		break;
	case AUDIO_QUALITY_PRESET_STANDARD:
		ui->filerate_PD->setCurrentIndex(0); // 48khz
		ui->bitDepthComboBox->setCurrentIndex(1); // 24 bit
		ui->videoExportAudioBitrateSpinBox->setValue(192);
		break;
	case AUDIO_QUALITY_PRESET_HIGH:
		ui->filerate_PD->setCurrentIndex(1); // 96khz
		ui->bitDepthComboBox->setCurrentIndex(1); // 24 bit
		ui->videoExportAudioBitrateSpinBox->setValue(320);
		break;
	case AUDIO_QUALITY_PRESET_CUSTOM:
	default:
		break;
	}
	audioQualityPresetSyncLock = false;

	UpdateAudioOnlyQualityPresetFromControls();
	SaveDefaultsAudio();
}

void MainWindow::on_audioOnlyQualityPresetComboBox_currentIndexChanged(int index)
{
	if(audioOnlyQualityPresetSyncLock) return;

	audioOnlyQualityPresetSyncLock = true;
	switch(index)
	{
	case AUDIO_QUALITY_PRESET_COMPACT:
		ui->filerate_PD->setCurrentIndex(0); // 48khz
		ui->bitDepthComboBox->setCurrentIndex(0); // 16 bit
		break;
	case AUDIO_QUALITY_PRESET_STANDARD:
		ui->filerate_PD->setCurrentIndex(0); // 48khz
		ui->bitDepthComboBox->setCurrentIndex(1); // 24 bit
		break;
	case AUDIO_QUALITY_PRESET_HIGH:
		ui->filerate_PD->setCurrentIndex(1); // 96khz
		ui->bitDepthComboBox->setCurrentIndex(1); // 24 bit
		break;
	case AUDIO_QUALITY_PRESET_CUSTOM:
	default:
		break;
	}
	audioOnlyQualityPresetSyncLock = false;

	UpdateAudioQualityPresetFromControls();
	SaveDefaultsAudio();
}

void MainWindow::on_filmrate_PD_currentIndexChanged(int index)
{
	Q_UNUSED(index);
	UpdateVideoExportControls();
}

void MainWindow::on_filerate_PD_currentIndexChanged(int index)
{
	Q_UNUSED(index);
	UpdateAudioOnlyQualityPresetFromControls();
	UpdateAudioQualityPresetFromControls();
}

void MainWindow::on_bitDepthComboBox_currentIndexChanged(int index)
{
	Q_UNUSED(index);
	UpdateAudioOnlyQualityPresetFromControls();
	UpdateAudioQualityPresetFromControls();
}

void MainWindow::on_videoAvOffsetCheckBox_toggled(bool checked)
{
	Q_UNUSED(checked);
	SaveExtractOutputSettings();
}

void MainWindow::on_outputRotationComboBox_currentIndexChanged(int index)
{
	Q_UNUSED(index);
	SaveExtractOutputSettings();
}

void MainWindow::on_videoExportModeComboBox_currentIndexChanged(int index)
{
	videoExportMode = ClampRangeInt(index, SIMPLE_EXPORT_FAST, SIMPLE_EXPORT_ULTRA);
	SaveVideoExportSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_videoExportTargetProfileComboBox_currentIndexChanged(int index)
{
	videoTargetProfileMode = ClampRangeInt(
			index,
			VIDEO_TARGET_PROFILE_CUSTOM,
			VIDEO_TARGET_PROFILE_ARCHIVE);
	if(videoTargetProfileMode != VIDEO_TARGET_PROFILE_CUSTOM)
	{
		ApplyVideoTargetProfile(videoTargetProfileMode);
		ApplyVideoContainerExtensionToOutputFilename();
		SaveExtractOutputSettings();
	}
	SaveVideoExportSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_videoExportResolutionComboBox_currentIndexChanged(int index)
{
	videoResolutionMode = ClampRangeInt(
			index, VIDEO_RESOLUTION_SOURCE, VIDEO_RESOLUTION_3840X2160);
	SaveVideoExportSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_videoExportContainerComboBox_currentIndexChanged(int index)
{
	videoContainerMode = ClampRangeInt(index, VIDEO_CONTAINER_MP4, VIDEO_CONTAINER_MKV);
	ApplyVideoContainerExtensionToOutputFilename();
	SaveVideoExportSettings();
	SaveExtractOutputSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_videoExportCodecComboBox_currentIndexChanged(int index)
{
	videoCodecMode = ClampRangeInt(index, VIDEO_CODEC_H264, VIDEO_CODEC_MPEG4);
	SaveVideoExportSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_videoExportPresetComboBox_currentIndexChanged(int index)
{
	videoPresetMode = ClampRangeInt(index, 0, 2);
	SaveVideoExportSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_videoExportQualitySlider_valueChanged(int value)
{
	videoQualityPercent = ClampRangeInt(value, 1, 100);
	SaveVideoExportSettings();
	UpdateVideoExportControls();
}

void MainWindow::on_videoExportAudioBitrateSpinBox_valueChanged(int arg1)
{
	videoAudioBitrateKbps = ClampRangeInt(arg1, 96, 512);
	SaveVideoExportSettings();
	UpdateVideoExportControls();
	UpdateAudioQualityPresetFromControls();
}

void MainWindow::on_queueBatchReportModeComboBox_currentIndexChanged(int index)
{
	queueBatchReportMode = ClampRangeInt(
			index, QUEUE_BATCH_REPORT_OFF, QUEUE_BATCH_REPORT_BOTH);
	SaveExtractOutputSettings();
}

void MainWindow::on_queuePauseResumeButton_clicked()
{
	if(!queueBatchRunning)
		return;
	queueBatchPaused = !queueBatchPaused;
	if(queueBatchPaused)
	{
		ui->queueBatchStatusLabel->setText(
				tr("Batch gepauzeerd. Klik Resume Batch om verder te gaan."));
	}
	else
	{
		ui->queueBatchStatusLabel->setText(
				tr("Batch hervat..."));
	}
	UpdateQueueWidgets();
}

void MainWindow::on_queueSkipCurrentButton_clicked()
{
	if(!queueBatchRunning)
		return;
	queueBatchSkipRequested = true;
	this->requestCancel = true;
	ui->queueBatchStatusLabel->setText(
			tr("Batch: skip huidig item aangevraagd..."));
}

void MainWindow::on_videoExportTestFfmpegButton_clicked()
{
	QString error;
	const QString testResult = RunFfmpegSelfTest(&error);
	if(error.isEmpty())
	{
		ui->videoExportFfmpegStatusLabel->setText(testResult);
		QMessageBox::information(
				this,
				tr("FFmpeg-test geslaagd"),
				tr("FFmpeg reageert correct.\n%1").arg(testResult));
	}
	else
	{
		ui->videoExportFfmpegStatusLabel->setText(
				tr("FFmpeg-check mislukt: %1").arg(error));
		QMessageBox::warning(
				this,
				tr("FFmpeg-test mislukt"),
				error);
	}
}

void MainWindow::on_autoOptimizeFramesCheckBox_toggled(bool checked)
{
	autoOptimizeFramesEnabled = checked;
	SaveVideoExportSettings();
	if(checked)
		ApplyAutoOptimizationToFrameControls(true);
	else
		UpdateAutoOptimizationControls();
}

void MainWindow::on_autoOptimizeVideoCheckBox_toggled(bool checked)
{
	autoOptimizeVideoEnabled = checked;
	SaveVideoExportSettings();
	UpdateAutoOptimizationControls();
}

void MainWindow::on_autoOptimizeColorCheckBox_toggled(bool checked)
{
	autoOptimizeColorEnabled = checked;
	SaveVideoExportSettings();
	if(autoOptimizeFramesEnabled)
		ApplyAutoOptimizationToFrameControls(true);
	else
		UpdateAutoOptimizationControls();
}

void MainWindow::on_autoOptimizeProfileComboBox_currentIndexChanged(int index)
{
	autoOptimizeProfileMode = ClampRangeInt(index, 0, 2);
	SaveVideoExportSettings();
	if(autoOptimizeFramesEnabled)
		ApplyAutoOptimizationToFrameControls(true);
	else
		UpdateAutoOptimizationControls();
}

void MainWindow::on_autoOptimizeAnalyzeButton_clicked()
{
	if(!currentFrameTexture)
	{
		QMessageBox::information(
				this,
				tr("Auto-optimalisatie"),
				tr("Laad eerst een bronframe om automatische analyse uit te voeren."));
		return;
	}

	const bool shouldApplyFrameAdjust = autoOptimizeFramesEnabled;
	if(shouldApplyFrameAdjust)
		ApplyAutoOptimizationToFrameControls(true);
	else
		UpdateAutoOptimizationControls();

	if(autoOptimizeVideoEnabled)
	{
		double b = 0.0, c = 1.0, s = 1.0;
		QString summary;
		if(ComputeAutoVideoEqSettings(b, c, s, &summary))
		{
			QMessageBox::information(
					this,
					tr("Video auto-optimalisatie"),
					summary);
		}
	}
}

