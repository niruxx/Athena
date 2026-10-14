#pragma once

#include <QDialog>
#include <QMessageBox>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

// Material 3 dialog: an optional icon, a headline, supporting text, the
// dialog's own content, and right-aligned actions, on a rounded,
// shadowed card. Under the Material look it draws its own frame (no
// desktop decoration; drag it by any empty area) and fades in; under the
// Breeze look it's an ordinary native dialog with the same layout.
class M3Dialog : public QDialog {
    Q_OBJECT

public:
    explicit M3Dialog(QWidget *parent = nullptr);

    void setHeadline(const QString &text, const QString &iconName = QString());
    void setSupportingText(const QString &text);
    // Where the dialog's own content goes, between the text and the actions.
    QVBoxLayout *body() const { return m_body; }
    // Adds an action button (right-aligned, in the order added). Roles are
    // MaterialTheme button roles: "text" for dismissive, "filled" for the
    // confirming action, "danger" for destructive ones.
    QPushButton *addAction(const QString &text, const QString &role, const QString &iconName = QString());
    QHBoxLayout *actionRow() const { return m_actions; }
    // Lets the user resize it (from the card's edges, when frameless).
    void setResizable(bool resizable);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    bool m_frameless;
    QWidget *m_content;
    QLabel *m_icon;
    QLabel *m_headline;
    QLabel *m_supporting;
    QVBoxLayout *m_body;
    QHBoxLayout *m_actions;
    class FramelessHelper *m_frameHelper = nullptr;
};

// Drop-in replacements for the QMessageBox static functions, as M3
// dialogs under the Material look (native QMessageBox under Breeze).
namespace M3Message {

QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                     QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
void about(QWidget *parent, const QString &title, const QString &text);

} // namespace M3Message
