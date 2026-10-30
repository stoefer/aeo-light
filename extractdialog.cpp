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
#include "extractdialog.h"
#include "ui_extractdialog.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QDebug>
#include <QMessageBox>
#include <QSettings>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

static int ClampInt(int v, int minv, int maxv)
{
	if(v < minv) return minv;
	if(v > maxv) return maxv;
	return v;
}

static QString ReadLastExportDir(const QString &key, const QString &fallback)
{
	QSettings settings;
	settings.beginGroup("default-folder");
	QString dir = settings.value(key, fallback).toString();
	settings.endGroup();
	if(dir.isEmpty()) dir = fallback;
	return dir;
}

static void WriteLastExportDir(const QString &key, const QString &dir)
{
	if(dir.isEmpty()) return;
	QSettings settings;
	settings.beginGroup("default-folder");
	settings.setValue(key, dir);
	// Keep the generic export folder aligned with the last audio output.
	if(key == "last-audio-export")
		settings.setValue("export", dir);
	settings.endGroup();
}

ExtractDialog::ExtractDialog(QWidget *parent, MetaData &metadata,
			const QString &dir) :
	QDialog(parent),
	ui(new Ui::ExtractDialog)
{
	ui->setupUi(this);

	meta = &metadata;
	defaultDir = dir;
	requestRestart = false;
	videoIsRisky = false;
	videoContainerMode = VIDEO_CONTAINER_MP4;
	videoCodecMode = VIDEO_CODEC_H264;
	videoQualityPercent = 85;
	videoAudioBitrateKbps = 192;
	videoPresetMode = 1;
	LoadVideoExportSettings();
	ui->videoPresetComboBox->setCurrentIndex(SIMPLE_EXPORT_STANDARD);
	ui->avOffsetCorrectionCheckBox->setChecked(true);
	ui->stableVideoCropCheckBox->setChecked(true);
	ui->flipHorizontalCheckBox->setChecked(false);
	ui->flipVerticalCheckBox->setChecked(false);
	ui->rotationComboBox->setCurrentIndex(0);

	SetOriginator(meta->originator);
	SetArchiveLocation(meta->archivalLocation);
	SetCopyrightInfo(meta->copyright);
	SetVersion(meta->version);
	SetCodingHistory(meta->codingHistory);
	SetTimeReferenceStr(QString("%L1").arg(meta->timeReference));

	ui->datetimeDisplay->setText(
			QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
	UpdateVideoAdvancedSummary();

}

ExtractDialog::~ExtractDialog()
{
	delete ui;
}

void ExtractDialog::MarkVideoAsRisky(void)
{
	ui->muxVideoCheckbox->setChecked(false);
	on_muxVideoCheckbox_clicked(false);
	videoIsRisky = true;
}

QString ExtractDialog::GetFilename(void)
{
	return ui->filenameText->text();
}

QString ExtractDialog::GetVideoFilename(void)
{
	if(ui->muxVideoCheckbox->isChecked())
		return ui->filenameVideoText->text();
	else
		return QString();
}

int ExtractDialog::GetVideoExportMode(void)
{
	if(ui->muxVideoCheckbox->isChecked())
		return ui->videoPresetComboBox->currentIndex();
	return SIMPLE_EXPORT_STANDARD;
}

int ExtractDialog::GetVideoContainerMode(void)
{
	return videoContainerMode;
}

int ExtractDialog::GetVideoCodecMode(void)
{
	return videoCodecMode;
}

int ExtractDialog::GetVideoQualityPercent(void)
{
	return videoQualityPercent;
}

int ExtractDialog::GetVideoAudioBitrateKbps(void)
{
	return videoAudioBitrateKbps;
}

int ExtractDialog::GetVideoPresetMode(void)
{
	return videoPresetMode;
}

bool ExtractDialog::UseVideoAudioOffsetCorrection(void)
{
	return ui->muxVideoCheckbox->isChecked() &&
			ui->avOffsetCorrectionCheckBox->isChecked();
}

bool ExtractDialog::UseStableVideoFrameCrop(void)
{
	return ui->muxVideoCheckbox->isChecked() &&
			ui->stableVideoCropCheckBox->isChecked();
}

bool ExtractDialog::GetVideoFlipHorizontal(void)
{
	return ui->muxVideoCheckbox->isChecked() &&
			ui->flipHorizontalCheckBox->isChecked();
}

bool ExtractDialog::GetVideoFlipVertical(void)
{
	return ui->muxVideoCheckbox->isChecked() &&
			ui->flipVerticalCheckBox->isChecked();
}

int ExtractDialog::GetVideoRotationDegrees(void)
{
	if(!ui->muxVideoCheckbox->isChecked()) return 0;
	switch(ui->rotationComboBox->currentIndex())
	{
	case 1: return 90;
	case 2: return 180;
	case 3: return 270;
	default: return 0;
	}
}

void ExtractDialog::SetStableVideoFrameCrop(bool enabled)
{
	ui->stableVideoCropCheckBox->setChecked(enabled);
}

QString ExtractDialog::GetOriginator(void)
{
	return ui->originatorDisplay->text();
}

QString ExtractDialog::GetOriginatorReference(void)
{
	return ui->originatorReferenceText->text();
}

QString ExtractDialog::GetDescription(void)
{
	return ui->descriptionText->text();
}

uint16_t ExtractDialog::GetVersion(void)
{
	return version;
}

QString ExtractDialog::GetCodingHistory(void)
{
	return ui->codingHistoryDisplay->text();
}

QString ExtractDialog::GetArchiveLocation(void)
{
	return ui->archiveLocationDisplay->text();
}

QString ExtractDialog::GetSubjectInfo(void)
{
	return ui->subjectText->text();
}

QString ExtractDialog::GetCopyrightInfo(void)
{
	return ui->copyrightText->text();
}

void ExtractDialog::SetFilename(const QString &filename)
{
	ui->filenameText->setText(filename);
}

void ExtractDialog::SetOriginator(const QString &originator)
{
	ui->originatorDisplay->setText(originator);
}

void ExtractDialog::SetOriginatorReference(const QString &originatorReference)
{
	ui->originatorReferenceText->setText(originatorReference);
}

void ExtractDialog::SetDescription(const QString &description)
{
	ui->descriptionText->setText(description);
}

void ExtractDialog::SetOriginationDateStr(const QString &datestr)
{
	ui->datetimeDisplay->setText(datestr);
}

void ExtractDialog::SetVersion(uint16_t version)
{
	ui->versionDisplay->setText(QString("%1h").arg(version, 4, 16, QChar('0')));
}

void ExtractDialog::SetTimeReferenceStr(const QString &timecode)
{
	ui->timecodeDisplay->setText(timecode);
}

void ExtractDialog::SetCodingHistory(const QString &codingHistory)
{
	ui->codingHistoryDisplay->setText(codingHistory);
}

void ExtractDialog::SetArchiveLocation(const QString &archiveLocation)
{
	ui->archiveLocationDisplay->setText(archiveLocation);
}

void ExtractDialog::SetSubjectInfo(const QString &subjectInfo)
{
	ui->subjectText->setText(subjectInfo);
}

void ExtractDialog::SetCopyrightInfo(const QString &copyrightInfo)
{
	ui->copyrightText->setText(copyrightInfo);
}

void ExtractDialog::on_cancelButton_clicked()
{
	reject();
}

void ExtractDialog::on_okButton_clicked()
{
	meta->description = ui->descriptionText->text();
	meta->originator = ui->originatorDisplay->text();
	meta->originatorReference = ui->originatorReferenceText->text();
	meta->comment = ui->subjectText->text();
	meta->copyright = ui->copyrightText->text();

	QFileInfo audioOut(ui->filenameText->text());
	if(!audioOut.absolutePath().isEmpty())
		WriteLastExportDir("last-audio-export", audioOut.absolutePath());

	if(ui->muxVideoCheckbox->isChecked())
	{
		ApplyContainerExtensionToVideoFilename();
		QFileInfo videoOut(ui->filenameVideoText->text());
		if(!videoOut.absolutePath().isEmpty())
			WriteLastExportDir("last-video-export", videoOut.absolutePath());
	}

	accept();
}

static QString SaveFile(QWidget *parent, QString label, QString defaultPath,
		QString current, QString ext)
{
	QString sel;

	if(!current.isEmpty())
	{
		if(QFileInfo(current).isRelative())
			sel = defaultPath + "/" + current;
		else
			sel = current;
	}
	else
	{
		sel = defaultPath;
	}

	return QFileDialog::getSaveFileName(parent, label, sel, ext);
}

void ExtractDialog::on_fileBrowseButton_clicked()
{
	QString browseDir = ReadLastExportDir("last-audio-export", defaultDir);
	QString filename = SaveFile(
			this, tr("Export audio to"), browseDir,
			ui->filenameText->text(), "*.wav");

	if(filename.isEmpty()) return;

	ui->filenameText->setText(filename);
	WriteLastExportDir("last-audio-export", QFileInfo(filename).absolutePath());

	if(ui->filenameVideoText->text().isEmpty())
	{
		QFileInfo fi(filename);
		ui->filenameVideoText->setText(
				fi.dir().path() + "/" + fi.completeBaseName() +
				VideoContainerExtension(videoContainerMode));
	}
}

void ExtractDialog::on_muxVideoCheckbox_clicked(bool checked)
{
	if(checked && videoIsRisky)
	{
		QMessageBox w(QMessageBox::Warning,
				tr("Video Instability Warning"),
				tr("The video creation code is unstable and may crash "
				   "the program when creating a video a second time.\n\n"
				   "Restart AEO-Light to avoid this."),
				QMessageBox::Cancel | QMessageBox::Ignore | QMessageBox::Abort,
				this);
		int answer;

		w.button(QMessageBox::Abort)->setText("Restart");
		w.setDefaultButton(QMessageBox::Cancel);

		answer = w.exec();

		if(answer == QMessageBox::Abort)
		{
			this->requestRestart = true;
			this->done(this->Rejected);
			return;
		}
		else if(answer == QMessageBox::Cancel)
		{
			ui->muxVideoCheckbox->setChecked(false);
			return;
		}
	}
	ui->filenameVideoText->setEnabled(checked);
	ui->fileVideoBrowseButton->setEnabled(checked);
	ui->videoPresetLabel->setEnabled(checked);
	ui->videoPresetComboBox->setEnabled(checked);
	ui->videoAdvancedButton->setEnabled(checked);
	ui->videoPresetHelpLabel->setEnabled(checked);
	ui->avOffsetCorrectionCheckBox->setEnabled(checked);
	ui->avOffsetHelpLabel->setEnabled(checked);
	ui->stableVideoCropLabel->setEnabled(checked);
	ui->stableVideoCropCheckBox->setEnabled(checked);
	ui->stableVideoCropHelpLabel->setEnabled(checked);
	ui->videoOrientationLabel->setEnabled(checked);
	ui->flipHorizontalCheckBox->setEnabled(checked);
	ui->flipVerticalCheckBox->setEnabled(checked);
	ui->rotationComboBox->setEnabled(checked);
	ui->previewOrientationPresetButton->setEnabled(checked);
}

void ExtractDialog::on_fileVideoBrowseButton_clicked()
{
	QString browseDir = ReadLastExportDir("last-video-export", defaultDir);
	QString filename = SaveFile(this, tr("Save video as"), browseDir,
			ui->filenameVideoText->text(), "*.mp4 *.mov *.mkv");

	if(filename.isEmpty()) return;

	ui->filenameVideoText->setText(filename);
	QString suffix = QFileInfo(filename).suffix().toLower();
	if(suffix == "mov")
		videoContainerMode = VIDEO_CONTAINER_MOV;
	else if(suffix == "mkv")
		videoContainerMode = VIDEO_CONTAINER_MKV;
	else
		videoContainerMode = VIDEO_CONTAINER_MP4;
	SaveVideoExportSettings();
	UpdateVideoAdvancedSummary();
	WriteLastExportDir("last-video-export", QFileInfo(filename).absolutePath());
}

void ExtractDialog::on_previewOrientationPresetButton_clicked()
{
	// Default preset to match the preview orientation in the current
	// external mp4 export path.
	ui->flipHorizontalCheckBox->setChecked(true);
	ui->flipVerticalCheckBox->setChecked(false);
	ui->rotationComboBox->setCurrentIndex(0); // 0 degrees
}

void ExtractDialog::on_videoAdvancedButton_clicked()
{
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Video & Optical Audio instellingen"));

	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *title = new QLabel(tr("Stel container, output-formaat en compressie in.\n"
								  "Hogere kwaliteit vermindert artefacten."), &dialog);
	title->setWordWrap(true);
	layout->addWidget(title);

	QFormLayout *form = new QFormLayout();

	QComboBox *containerCombo = new QComboBox(&dialog);
	containerCombo->addItem("MP4");
	containerCombo->addItem("MOV");
	containerCombo->addItem("MKV");
	containerCombo->setCurrentIndex(videoContainerMode);

	QComboBox *codecCombo = new QComboBox(&dialog);
	codecCombo->addItem("H.264 (aanbevolen)");
	codecCombo->addItem("HEVC / H.265");
	codecCombo->addItem("MPEG-4 (legacy)");
	codecCombo->setCurrentIndex(videoCodecMode);

	QComboBox *presetCombo = new QComboBox(&dialog);
	presetCombo->addItem("Sneller encoderen");
	presetCombo->addItem("Gebalanceerd");
	presetCombo->addItem("Langzamer, beste compressie");
	presetCombo->setCurrentIndex(videoPresetMode);

	QSlider *qualitySlider = new QSlider(Qt::Horizontal, &dialog);
	qualitySlider->setRange(1, 100);
	qualitySlider->setValue(videoQualityPercent);
	QLabel *qualityValueLabel = new QLabel(
			QString("%1%").arg(videoQualityPercent), &dialog);
	QHBoxLayout *qualityLayout = new QHBoxLayout();
	qualityLayout->addWidget(qualitySlider, 1);
	qualityLayout->addWidget(qualityValueLabel);

	QSpinBox *audioBitrateSpin = new QSpinBox(&dialog);
	audioBitrateSpin->setRange(96, 512);
	audioBitrateSpin->setSingleStep(16);
	audioBitrateSpin->setSuffix(" kbps");
	audioBitrateSpin->setValue(videoAudioBitrateKbps);

	QObject::connect(qualitySlider, &QSlider::valueChanged,
			[qualityValueLabel](int value)
	{
		qualityValueLabel->setText(QString("%1%").arg(value));
	});

	form->addRow(tr("Container"), containerCombo);
	form->addRow(tr("Output formaat"), codecCombo);
	form->addRow(tr("Encoder preset"), presetCombo);
	form->addRow(tr("Video kwaliteit"), qualityLayout);
	form->addRow(tr("Audio bitrate"), audioBitrateSpin);
	layout->addLayout(form);

	QLabel *hint = new QLabel(tr("Tip: start met H.264 + MP4 + kwaliteit 85% voor minder artefacten."),
			&dialog);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::accepted,
			&dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected,
			&dialog, &QDialog::reject);

	if(dialog.exec() != QDialog::Accepted)
		return;

	videoContainerMode = containerCombo->currentIndex();
	videoCodecMode = codecCombo->currentIndex();
	videoPresetMode = presetCombo->currentIndex();
	videoQualityPercent = qualitySlider->value();
	videoAudioBitrateKbps = audioBitrateSpin->value();

	SaveVideoExportSettings();
	ApplyContainerExtensionToVideoFilename();
	UpdateVideoAdvancedSummary();
}

void ExtractDialog::LoadVideoExportSettings()
{
	QSettings settings;
	settings.beginGroup("video-export");
	videoContainerMode = ClampInt(
				settings.value("container", VIDEO_CONTAINER_MP4).toInt(),
				VIDEO_CONTAINER_MP4, VIDEO_CONTAINER_MKV);
	videoCodecMode = ClampInt(
				settings.value("codec", VIDEO_CODEC_H264).toInt(),
				VIDEO_CODEC_H264, VIDEO_CODEC_MPEG4);
	videoQualityPercent = ClampInt(
				settings.value("quality-percent", 85).toInt(),
				1, 100);
	videoAudioBitrateKbps = ClampInt(
				settings.value("audio-bitrate-kbps", 192).toInt(),
				96, 512);
	videoPresetMode = ClampInt(
				settings.value("preset-mode", 1).toInt(),
				0, 2);
	settings.endGroup();
}

void ExtractDialog::SaveVideoExportSettings()
{
	QSettings settings;
	settings.beginGroup("video-export");
	settings.setValue("container", videoContainerMode);
	settings.setValue("codec", videoCodecMode);
	settings.setValue("quality-percent", videoQualityPercent);
	settings.setValue("audio-bitrate-kbps", videoAudioBitrateKbps);
	settings.setValue("preset-mode", videoPresetMode);
	settings.endGroup();
}

QString ExtractDialog::VideoContainerExtension(int mode) const
{
	switch(mode)
	{
	case VIDEO_CONTAINER_MOV: return ".mov";
	case VIDEO_CONTAINER_MKV: return ".mkv";
	case VIDEO_CONTAINER_MP4:
	default: return ".mp4";
	}
}

void ExtractDialog::ApplyContainerExtensionToVideoFilename()
{
	QString current = ui->filenameVideoText->text();
	if(current.isEmpty()) return;

	QFileInfo info(current);
	QString adjusted = info.path() + "/" + info.completeBaseName() +
			VideoContainerExtension(videoContainerMode);
	ui->filenameVideoText->setText(adjusted);
}

void ExtractDialog::UpdateVideoAdvancedSummary()
{
	static const char *containers[] = { "MP4", "MOV", "MKV" };
	static const char *codecs[] = { "H.264", "HEVC", "MPEG-4" };
	static const char *presets[] = { "Sneller", "Gebalanceerd", "Langzamer" };

	QString summary = tr("Tip voor beginners: kies \"Standaard\".\n"
						 "Uitgebreid: %1, %2, preset %3, kwaliteit %4%, audio %5 kbps.")
			.arg(containers[ClampInt(videoContainerMode, 0, 2)])
			.arg(codecs[ClampInt(videoCodecMode, 0, 2)])
			.arg(presets[ClampInt(videoPresetMode, 0, 2)])
			.arg(videoQualityPercent)
			.arg(videoAudioBitrateKbps);
	ui->videoPresetHelpLabel->setText(summary);
}
