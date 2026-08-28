#pragma once

#include <QWidget>

class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

private slots:
    void onThemeIndexChanged(int index);
    void onStartupTabIndexChanged(int index);
    void onAutoCloseToggled(bool checked);
    void onCheckForUpdatesToggled(bool checked);
    void onCompactListsToggled(bool checked);
    void onAutoConfirmToggled(bool checked);
    void onHideTrayToggled(bool checked);
    void onUpdateIntervalChanged(int minutes);
    void onMetadataExpireChanged(int hours);
    void onDisableGroupViewToggled(bool checked);
    void onLoggingEnabledToggled(bool checked);
    void onBrowseLogDirectory();
    void onLogLevelIndexChanged(int index);

private:
    QComboBox *m_themeCombo;
    QComboBox *m_startupTabCombo;
    QCheckBox *m_autoCloseCheck;
    QCheckBox *m_checkForUpdatesCheck;
    QCheckBox *m_compactListsCheck;

    QCheckBox *m_autoConfirmCheck;
    QCheckBox *m_hideTrayCheck;
    QSpinBox *m_updateIntervalSpin;
    QSpinBox *m_metadataExpireSpin;

    QCheckBox *m_disableGroupViewCheck;

    QCheckBox *m_loggingEnabledCheck;
    QLineEdit *m_logDirectoryEdit;
    QComboBox *m_logLevelCombo;
};
