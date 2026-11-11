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
#include "preferencesdialog.h"
#include "ui_preferencesdialog.h"

#include <algorithm>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSettings>
#include <QDebug>
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QTabWidget>

static bool ReadShowHelpTextPreference(QSettings &settings, bool defaultValue = true)
{
	settings.beginGroup("ui");
	const bool hasUiSetting = settings.contains("show-help-text");
	bool showHelpText = settings.value("show-help-text", defaultValue).toBool();
	settings.endGroup();

	if(!hasUiSetting)
	{
		settings.beginGroup("extraction");
		if(settings.contains("show-audio-help-text"))
			showHelpText = settings.value("show-audio-help-text", defaultValue).toBool();
		settings.endGroup();
	}

	return showHelpText;
}

preferencesdialog::preferencesdialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::preferencesdialog),
	settingsSearchEdit(NULL),
	compactModeCheckBox(NULL),
	skipLicenseAgreementCheckBox(NULL),
	embeddedPreviewCheckBox(NULL),
	embeddedPreviewBadgeColorComboBox(NULL)
{
	ui->setupUi(this);

	settings = new QSettings;

	// Open the preferences dialog taller by default so more options are visible
	// before scrolling, while still fitting on smaller displays.
	const int preferredDialogHeight = 470;
	int targetHeight = std::max(height(), preferredDialogHeight);
	QScreen *screenForSize = this->screen();
	if(!screenForSize) screenForSize = QGuiApplication::primaryScreen();
	if(screenForSize)
	{
		const int maxSafeHeight = std::max(320,
				screenForSize->availableGeometry().height() - 60);
		targetHeight = std::min(targetHeight, maxSafeHeight);
	}
	if(targetHeight > height())
	{
		const int delta = targetHeight - height();
		resize(width(), targetHeight);
		if(ui->horizontalLayoutWidget)
		{
			QRect buttonRect = ui->horizontalLayoutWidget->geometry();
			buttonRect.moveTop(buttonRect.top() + delta);
			ui->horizontalLayoutWidget->setGeometry(buttonRect);
		}
		if(ui->tabWidget)
		{
			QRect tabRectGrow = ui->tabWidget->geometry();
			tabRectGrow.setHeight(tabRectGrow.height() + delta);
			ui->tabWidget->setGeometry(tabRectGrow);
		}
	}

	// Add a simple searchable filter so users can quickly jump to options.
	QLabel *searchLabel = new QLabel("Zoeken:", this);
	searchLabel->setGeometry(16, 44, 64, 24);
	settingsSearchEdit = new QLineEdit(this);
	settingsSearchEdit->setGeometry(80, 42, 330, 28);
	settingsSearchEdit->setPlaceholderText("Filter op optie-naam...");
	settingsSearchEdit->setClearButtonEnabled(true);
	QRect tabRect = ui->tabWidget->geometry();
	ui->tabWidget->setGeometry(
			tabRect.x(), tabRect.y() + 32,
			tabRect.width(), std::max(120, tabRect.height() - 32));

	// Make the General tab scrollable so added preference options never
	// get clipped behind the tab panel frame on smaller windows/scales.
	if(ui->verticalLayoutWidget && ui->generalTab)
	{
		QWidget *generalContent = ui->verticalLayoutWidget;
		const QRect contentRect = generalContent->geometry();
		QScrollArea *generalScrollArea = new QScrollArea(ui->generalTab);
		generalScrollArea->setObjectName("generalScrollArea");
		generalScrollArea->setGeometry(contentRect);
		generalScrollArea->setWidgetResizable(true);
		generalScrollArea->setFrameShape(QFrame::NoFrame);
		generalScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		generalContent->setParent(generalScrollArea);
		generalScrollArea->setWidget(generalContent);
	}

	QStringList l;

	l = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);

	if(l.size()) sysRead = l.at(0);
	else sysRead = "/";

	sysWrite = QStandardPaths::writableLocation(
			QStandardPaths::DocumentsLocation);

	settings->beginGroup("default-folder");
	ui->sourceText->setText(settings->value("source", sysRead).toString());
	ui->projectText->setText(settings->value("project", sysWrite).toString());
    ui->exportText->setText(settings->value("export", sysWrite).toString());
    ui->importText->setText(settings->value("import", sysRead).toString());
    settings->endGroup();

	const bool showHelpText = ReadShowHelpTextPreference(*settings, true);
	settings->beginGroup("ui");
	ui->skipCloseConfirmCheckBox->setChecked(
			settings->value("skip-close-confirmation", false).toBool());
	ui->showHelpTextCheckBox->setChecked(showHelpText);
	const bool compactModeEnabled =
			settings->value("compact-mode", false).toBool();
	const bool skipLicenseAgreement =
			settings->value("skip-license-agreement", false).toBool();
	const bool embeddedPreviewEnabled =
			settings->value("embed-preview-beta", false).toBool();
	const QString embeddedPreviewBadgeColor =
			settings->value("embed-preview-badge-color", "purple")
					.toString().toLower().trimmed();
	QString theme = settings->value("theme", "light").toString().toLower();
	ui->themeComboBox->setCurrentIndex(theme == "dark" ? 1 : 0);
	QString mode = settings->value("window-layout-mode", "").toString().toLower();
	if(mode.isEmpty())
	{
		bool legacyDual = settings->value("window-layout-dual-screen", false).toBool();
		bool legacySingle = settings->value("window-layout-single-fill", false).toBool();
		if(legacyDual)
			mode = "dual";
		else if(legacySingle)
			mode = "single";
		else
			mode = "standard";
	}
	if(mode == "single")
		ui->windowLayoutModeComboBox->setCurrentIndex(1);
	else if(mode == "dual")
		ui->windowLayoutModeComboBox->setCurrentIndex(2);
	else
		ui->windowLayoutModeComboBox->setCurrentIndex(0);
	ui->windowLayoutSwapCheckBox->setChecked(
			settings->value("window-layout-dual-swap", false).toBool());
	settings->endGroup();

	compactModeCheckBox = new QCheckBox(
			"Compact mode (minder tekst, compactere weergave)",
			ui->verticalLayoutWidget);
	compactModeCheckBox->setChecked(compactModeEnabled);
	ui->verticalLayout->insertWidget(3, compactModeCheckBox);
	skipLicenseAgreementCheckBox = new QCheckBox(
			"License agreement na eerste acceptatie niet meer tonen",
			ui->verticalLayoutWidget);
	skipLicenseAgreementCheckBox->setChecked(skipLicenseAgreement);
	ui->verticalLayout->insertWidget(6, skipLicenseAgreementCheckBox);
	embeddedPreviewCheckBox = new QCheckBox(
			"Preview insluiten in hoofdpaneel (beta, herstart aanbevolen)",
			ui->verticalLayoutWidget);
	embeddedPreviewCheckBox->setChecked(embeddedPreviewEnabled);
	ui->verticalLayout->insertWidget(7, embeddedPreviewCheckBox);
	QLabel *embeddedPreviewBadgeColorLabel = new QLabel(
			"Kleur BETA-badge (ingesloten preview):", ui->verticalLayoutWidget);
	embeddedPreviewBadgeColorComboBox = new QComboBox(ui->verticalLayoutWidget);
	embeddedPreviewBadgeColorComboBox->addItem(
			"Automatisch op basis van thema", "auto");
	embeddedPreviewBadgeColorComboBox->addItem("Paars (standaard)", "purple");
	embeddedPreviewBadgeColorComboBox->addItem("Blauw", "blue");
	embeddedPreviewBadgeColorComboBox->addItem("Groen", "green");
	embeddedPreviewBadgeColorComboBox->addItem("Oranje", "orange");
	embeddedPreviewBadgeColorComboBox->addItem("Rood", "red");
	embeddedPreviewBadgeColorComboBox->addItem("Grijs", "gray");
	int badgeColorIndex = embeddedPreviewBadgeColorComboBox->findData(
			embeddedPreviewBadgeColor);
	if(badgeColorIndex < 0) badgeColorIndex = 0;
	embeddedPreviewBadgeColorComboBox->setCurrentIndex(badgeColorIndex);
	embeddedPreviewBadgeColorLabel->setEnabled(embeddedPreviewEnabled);
	embeddedPreviewBadgeColorComboBox->setEnabled(embeddedPreviewEnabled);
	ui->verticalLayout->insertWidget(8, embeddedPreviewBadgeColorLabel);
	ui->verticalLayout->insertWidget(9, embeddedPreviewBadgeColorComboBox);

	PopulateMonitorChoices();
	settings->beginGroup("ui");
	int targetMonitor = settings->value("window-layout-target-monitor", 0).toInt();
	settings->endGroup();
	int monitorComboIndex = ui->windowLayoutMonitorComboBox->findData(targetMonitor);
	if(monitorComboIndex < 0) monitorComboIndex = 0;
	ui->windowLayoutMonitorComboBox->setCurrentIndex(monitorComboIndex);
	UpdateWindowLayoutControls();
	InstallInlineHelpIcons();
	ApplyHelpTextVisibility(showHelpText);

	ui->sourceText->setPlaceholderText(sysRead);
	ui->projectText->setPlaceholderText(sysWrite);
    ui->exportText->setPlaceholderText(sysWrite);
    ui->importText->setPlaceholderText(sysRead);

    connect(
        ui->browseForSourceButton, &QPushButton::clicked,
        this, [this](){
            BrowseForFolder(
                ui->sourceText,
                "Default Source Scan Folder",
                sysRead);
            }
        );
    connect(
        ui->browseForProjectButton, &QPushButton::clicked,
        this, [this](){
            BrowseForFolder(
                ui->projectText,
                "Default AEO Project File Folder",
                sysWrite);
        }
        );
    connect(
        ui->browseForExportButton, &QPushButton::clicked,
        this, [this](){
            BrowseForFolder(
                ui->exportText,
                "Default Export Folder",
                sysWrite);
        }
        );
    connect(
        ui->browseForImportButton, &QPushButton::clicked,
        this, [this](){
            BrowseForFolder(
                ui->importText,
                "Default Import Folder",
                sysRead);
        }
        );

    connect(
        ui->sourceText, &QLineEdit::editingFinished,
        this, [this](){ ValidateFolder(sysRead); } );
    connect(
        ui->projectText, &QLineEdit::editingFinished,
        this, [this](){ ValidateFolder(sysWrite); } );
    connect(
        ui->exportText, &QLineEdit::editingFinished,
        this, [this](){ ValidateFolder(sysWrite); } );
    connect(
        ui->importText, &QLineEdit::editingFinished,
        this, [this](){ ValidateFolder(sysRead); } );


    connect(
        ui->discardButton, &QPushButton::clicked,
        this, &preferencesdialog::reject);
    connect(
        ui->saveButton, &QPushButton::clicked,
        this, &preferencesdialog::Save);
	connect(
		ui->windowLayoutModeComboBox,
		static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &preferencesdialog::UpdateWindowLayoutControls);
	connect(ui->showHelpTextCheckBox, &QCheckBox::toggled,
			this, &preferencesdialog::ApplyHelpTextVisibility);
	connect(settingsSearchEdit, &QLineEdit::textChanged,
			this, &preferencesdialog::ApplySettingsSearchFilter);
	connect(embeddedPreviewCheckBox, &QCheckBox::toggled,
			embeddedPreviewBadgeColorLabel, &QWidget::setEnabled);
	connect(embeddedPreviewCheckBox, &QCheckBox::toggled,
			embeddedPreviewBadgeColorComboBox, &QWidget::setEnabled);
}

preferencesdialog::~preferencesdialog()
{
	settings->sync();
	delete settings;

	delete ui;
}

void preferencesdialog::BrowseForFolder(
    QLineEdit *lineEdit, QString title, QString dflt)
{
    QString curdir = lineEdit->text();
    if(curdir.isEmpty()) curdir = dflt;

    QString dir = QFileDialog::getExistingDirectory(this, title, curdir);
    if(dir.isEmpty()) return;

    lineEdit->setText(dir);
}

void preferencesdialog::ValidateFolder(QString dflt)
{
    QLineEdit *w = qobject_cast<QLineEdit *>(sender());
    if(!w) return;

    if(w->text().isEmpty())
    {
        w->setText(dflt);
        return;
    }

	// turn off signals while we handle the message dialogs. This is a
	// workaround for the known QT bug wherein editingFinished is called
	// twice when a messagebox is shown:
	// https://bugreports.qt.io/browse/QTBUG-40
	w->blockSignals(true);

	QString txt = w->text();

	// for preference-file specs, treat all entries as absolute paths
	if(QDir(txt).isRelativePath(txt))
	{
		txt = QDir().rootPath() + txt;
		w->setText(txt);
	}

	if(!QDir(w->text()).exists())
	{
		QMessageBox msgBox;
		msgBox.setWindowTitle("Folder doesn't exist");
		msgBox.setText(QString("The folder '%1' doesn't exist.").arg(txt));
		msgBox.addButton("Create Folder", QMessageBox::AcceptRole);
		msgBox.addButton(QMessageBox::Cancel);
		int ret = msgBox.exec();
		if(ret != QMessageBox::Cancel)
		{
			if(!QDir().mkpath(w->text()))
			{
				QMessageBox::warning(NULL, "Could not create",
						"Could not create directory");
				ret = QMessageBox::Cancel;
			}
		}

		if(ret == QMessageBox::Cancel)
		{
			w->setFocus();
			w->selectAll();
		}
	}

	w->blockSignals(false);
}


void preferencesdialog::Save()
{
	settings->beginGroup("default-folder");
	settings->setValue("source",ui->sourceText->text());
	settings->setValue("project",ui->projectText->text());
    settings->setValue("export",ui->exportText->text());
    settings->setValue("import",ui->importText->text());
    settings->endGroup();

	settings->beginGroup("audio-metadata");
	settings->setValue("originator", ui->originatorText->text());
	settings->setValue("archive-location", ui->archiveLocationText->text());
	settings->setValue("copyright", ui->copyrightText->text());
	settings->endGroup();

	settings->beginGroup("ui");
	settings->setValue(
			"skip-close-confirmation",
			ui->skipCloseConfirmCheckBox->isChecked());
	settings->setValue(
			"show-help-text",
			ui->showHelpTextCheckBox->isChecked());
	settings->setValue(
			"compact-mode",
			compactModeCheckBox ? compactModeCheckBox->isChecked() : false);
	settings->setValue(
			"skip-license-agreement",
			skipLicenseAgreementCheckBox ?
					skipLicenseAgreementCheckBox->isChecked() : false);
	settings->setValue(
			"embed-preview-beta",
			embeddedPreviewCheckBox ?
					embeddedPreviewCheckBox->isChecked() : false);
	settings->setValue(
			"embed-preview-badge-color",
			embeddedPreviewBadgeColorComboBox ?
					embeddedPreviewBadgeColorComboBox->currentData().toString() :
					QString("purple"));
	settings->setValue(
			"theme",
			ui->themeComboBox->currentIndex() == 1 ? "dark" : "light");
	const QString layoutMode = CurrentWindowLayoutModeKey();
	settings->setValue("window-layout-mode", layoutMode);
	settings->setValue(
			"window-layout-dual-swap",
			ui->windowLayoutSwapCheckBox->isChecked());
	settings->setValue(
			"window-layout-target-monitor",
			ui->windowLayoutMonitorComboBox->currentData().toInt());

	// Keep legacy keys aligned for backwards compatibility.
	settings->setValue(
			"window-layout-dual-screen",
			layoutMode == "dual");
	settings->setValue(
			"window-layout-single-fill",
			layoutMode == "single");
	settings->endGroup();

	accept();
	//done(Accepted);
}

void preferencesdialog::PopulateMonitorChoices()
{
	ui->windowLayoutMonitorComboBox->clear();
	const QList<QScreen*> screens = QGuiApplication::screens();
	for(int i = 0; i < screens.size(); ++i)
	{
		QScreen *screen = screens.at(i);
		QString label = QString("Monitor %1").arg(i + 1);
		QString screenName = screen ? screen->name().trimmed() : QString();
		if(!screenName.isEmpty())
			label += QString(" - %1").arg(screenName);
		ui->windowLayoutMonitorComboBox->addItem(label, i);
	}
	if(ui->windowLayoutMonitorComboBox->count() == 0)
		ui->windowLayoutMonitorComboBox->addItem("Monitor 1", 0);
}

QString preferencesdialog::CurrentWindowLayoutModeKey() const
{
	switch(ui->windowLayoutModeComboBox->currentIndex())
	{
	case 1: return "single";
	case 2: return "dual";
	default: return "standard";
	}
}

void preferencesdialog::UpdateWindowLayoutControls()
{
	const QString mode = CurrentWindowLayoutModeKey();
	ui->windowLayoutMonitorLabel->setEnabled(mode == "single");
	ui->windowLayoutMonitorComboBox->setEnabled(mode == "single");
	ui->windowLayoutSwapCheckBox->setEnabled(mode == "dual");
}

void preferencesdialog::ApplyHelpTextVisibility(bool visible)
{
	const QList<QLabel*> labels = findChildren<QLabel*>();
	for(QLabel *label : labels)
	{
		if(!label) continue;
		if(label->objectName().contains("HelpLabel"))
			label->setVisible(visible);
	}
}

void preferencesdialog::ApplySettingsSearchFilter(const QString &term)
{
	const QString needle = term.trimmed().toLower();
	if(needle.isEmpty())
	{
		for(int i = 0; i < ui->tabWidget->count(); ++i)
			ui->tabWidget->setTabEnabled(i, true);
		if(settingsSearchEdit)
			settingsSearchEdit->setStyleSheet(QString());
		return;
	}

	auto tabMatches = [&](QWidget *tab) -> bool
	{
		if(!tab) return false;
		const QList<QWidget*> widgets = tab->findChildren<QWidget*>();
		for(QWidget *widget : widgets)
		{
			if(!widget) continue;
			QStringList haystacks;
			haystacks << widget->toolTip() << widget->whatsThis() <<
					widget->objectName();
			if(QLabel *label = qobject_cast<QLabel*>(widget))
				haystacks << label->text();
			if(QAbstractButton *button = qobject_cast<QAbstractButton*>(widget))
				haystacks << button->text();
			if(QLineEdit *lineEdit = qobject_cast<QLineEdit*>(widget))
			{
				haystacks << lineEdit->placeholderText()
						<< lineEdit->text();
			}
			if(QComboBox *combo = qobject_cast<QComboBox*>(widget))
			{
				for(int i = 0; i < combo->count(); ++i)
					haystacks << combo->itemText(i);
			}
			for(const QString &candidate : haystacks)
			{
				if(candidate.toLower().contains(needle))
					return true;
			}
		}
		return false;
	};

	int firstMatchingTab = -1;
	bool anyMatch = false;
	for(int i = 0; i < ui->tabWidget->count(); ++i)
	{
		QWidget *tab = ui->tabWidget->widget(i);
		const bool match = tabMatches(tab);
		ui->tabWidget->setTabEnabled(i, match);
		if(match)
		{
			anyMatch = true;
			if(firstMatchingTab < 0)
				firstMatchingTab = i;
		}
	}

	if(anyMatch && firstMatchingTab >= 0)
	{
		ui->tabWidget->setCurrentIndex(firstMatchingTab);
		if(settingsSearchEdit)
			settingsSearchEdit->setStyleSheet(QString());
		return;
	}

	// Keep tabs accessible when no result is found.
	for(int i = 0; i < ui->tabWidget->count(); ++i)
		ui->tabWidget->setTabEnabled(i, true);
	if(settingsSearchEdit)
		settingsSearchEdit->setStyleSheet(
				"QLineEdit { border: 1px solid #c85c5c; }");
}

void preferencesdialog::InstallInlineHelpIcons()
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
