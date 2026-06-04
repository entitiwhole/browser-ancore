#include "settingsdialog.h"
#include "browsercore.h"
#include "adblocker.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Настройки AnCore");
    setMinimumSize(500, 400);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);

    // === General Tab ===
    auto* generalWidget = new QWidget();
    auto* generalLayout = new QFormLayout(generalWidget);

    m_homePageEdit = new QLineEdit(generalWidget);
    m_homePageEdit->setPlaceholderText("ancore:newtab");
    generalLayout->addRow("Домашняя страница:", m_homePageEdit);

    m_searchEngineCombo = new QComboBox(generalWidget);
    m_searchEngineCombo->addItem("Google", "google");
    m_searchEngineCombo->addItem("Yandex", "yandex");
    m_searchEngineCombo->addItem("Bing", "bing");
    m_searchEngineCombo->addItem("DuckDuckGo", "duckduckgo");
    generalLayout->addRow("Поисковая система:", m_searchEngineCombo);

    m_downloadPathEdit = new QLineEdit(generalWidget);
    auto* downloadLayout = new QHBoxLayout();
    downloadLayout->addWidget(m_downloadPathEdit, 1);
    auto* browseBtn = new QPushButton("Обзор...", generalWidget);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getExistingDirectory(this,
            "Папка для загрузок", m_downloadPathEdit->text());
        if (!path.isEmpty())
            m_downloadPathEdit->setText(path);
    });
    downloadLayout->addWidget(browseBtn);
    generalLayout->addRow("Папка загрузок:", downloadLayout);

    m_tabs->addTab(generalWidget, "Основные");

    // === Privacy Tab ===
    auto* privacyWidget = new QWidget();
    auto* privacyLayout = new QVBoxLayout(privacyWidget);

    auto* privacyGroup = new QGroupBox("Конфиденциальность", privacyWidget);
    auto* privacyForm = new QFormLayout(privacyGroup);

    m_adBlockCheck = new QCheckBox("Включить блокировку рекламы", privacyWidget);
    privacyForm->addRow(m_adBlockCheck);

    m_doNotTrackCheck = new QCheckBox("Отправлять заголовок Do Not Track", privacyWidget);
    privacyForm->addRow(m_doNotTrackCheck);

    m_blockThirdPartyCookies = new QCheckBox("Блокировать сторонние cookie", privacyWidget);
    privacyForm->addRow(m_blockThirdPartyCookies);

    privacyLayout->addWidget(privacyGroup);
    privacyLayout->addStretch();

    m_tabs->addTab(privacyWidget, "Приватность");

    // === Performance Tab ===
    auto* perfWidget = new QWidget();
    auto* perfLayout = new QFormLayout(perfWidget);

    m_maxTabsSpin = new QSpinBox(perfWidget);
    m_maxTabsSpin->setRange(8, 256);
    m_maxTabsSpin->setValue(32);
    m_maxTabsSpin->setSuffix(" вкладок");
    perfLayout->addRow("Максимум вкладок:", m_maxTabsSpin);

    m_tabs->addTab(perfWidget, "Производительность");

    mainLayout->addWidget(m_tabs, 1);

    // === Buttons ===
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
        this, &SettingsDialog::saveSettings);

    mainLayout->addWidget(buttons);
}

void SettingsDialog::loadSettings()
{
    auto* core = BrowserCore::instance();

    m_homePageEdit->setText(core->homePage());

    int idx = m_searchEngineCombo->findData(core->searchEngine());
    if (idx >= 0) m_searchEngineCombo->setCurrentIndex(idx);

    m_downloadPathEdit->setText(core->downloadPath());
    m_adBlockCheck->setChecked(core->isAdBlockEnabled());
    m_doNotTrackCheck->setChecked(core->settings()->value("privacy/dnt", false).toBool());
    m_blockThirdPartyCookies->setChecked(core->settings()->value("privacy/block3rdparty", true).toBool());
    m_maxTabsSpin->setValue(core->settings()->value("performance/maxtabs", 32).toInt());
}

void SettingsDialog::saveSettings()
{
    auto* core = BrowserCore::instance();

    core->setHomePage(m_homePageEdit->text());
    core->setSearchEngine(m_searchEngineCombo->currentData().toString());
    core->setDownloadPath(m_downloadPathEdit->text());
    core->setAdBlockEnabled(m_adBlockCheck->isChecked());
    core->adBlocker()->setEnabled(m_adBlockCheck->isChecked());
    core->settings()->setValue("privacy/dnt", m_doNotTrackCheck->isChecked());
    core->settings()->setValue("privacy/block3rdparty", m_blockThirdPartyCookies->isChecked());
    core->settings()->setValue("performance/maxtabs", m_maxTabsSpin->value());

    emit core->settingsChanged();
}
