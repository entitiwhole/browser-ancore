#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    void setupUI();
    void loadSettings();
    void saveSettings();

    QTabWidget* m_tabs;
    QLineEdit* m_homePageEdit;
    QComboBox* m_searchEngineCombo;
    QLineEdit* m_downloadPathEdit;
    QCheckBox* m_adBlockCheck;
    QCheckBox* m_doNotTrackCheck;
    QCheckBox* m_blockThirdPartyCookies;
    QSpinBox* m_maxTabsSpin;
};
