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
				fi.dir().path() + "/" + fi.completeBaseName() + ".mp4");
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
			ui->filenameVideoText->text(), "*.mp4");

	if(filename.isEmpty()) return;

	ui->filenameVideoText->setText(filename);
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
