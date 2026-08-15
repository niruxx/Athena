#pragma once

#include <QWidget>

class QComboBox;
class QCheckBox;

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

private:
    QComboBox *m_themeCombo;
    QComboBox *m_startupTabCombo;
    QCheckBox *m_autoCloseCheck;
    QCheckBox *m_checkForUpdatesCheck;
    QCheckBox *m_compactListsCheck;
};
