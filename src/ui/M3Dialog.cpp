#include "M3Dialog.h"

#include <algorithm>

#include <QGraphicsEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QPushButton>
#include <QVBoxLayout>

#include "FramelessHelper.h"
#include "MaterialTheme.h"
#include "Motion.h"

namespace {
// Room around the card for its shadow (the frameless window is this much
// larger than the card on every side).
constexpr int kShadow = 22;
constexpr int kRadius = 28;

// The dialog's card: draws the rounded surface itself (when frameless) so
// the open animation can scale and fade card and contents together.
class DialogCard : public QWidget {
public:
    DialogCard(QWidget *parent, bool frameless) : QWidget(parent), m_frameless(frameless) { }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_frameless)
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(MaterialTheme::current().surfaceContainerHigh);
        painter.drawRoundedRect(QRectF(rect()), kRadius, kRadius);
    }

private:
    bool m_frameless;
};

// Material's dialog entrance: grow from 92% while fading in. A graphics
// effect, so it works inside the window on every platform (Wayland has no
// window-opacity animation).
class ScaleFadeEffect : public QGraphicsEffect {
public:
    explicit ScaleFadeEffect(QObject *parent) : QGraphicsEffect(parent) { }
    void setProgress(qreal progress)
    {
        m_progress = progress;
        update();
    }

protected:
    void draw(QPainter *painter) override
    {
        QPoint offset;
        const QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset, QGraphicsEffect::PadToEffectiveBoundingRect);
        const qreal scale = 0.92 + 0.08 * m_progress;
        const QRectF bounds(offset, pixmap.deviceIndependentSize());
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->setOpacity(m_progress);
        painter->translate(bounds.center());
        painter->scale(scale, scale);
        painter->translate(-bounds.center());
        painter->drawPixmap(offset, pixmap);
        painter->restore();
    }

private:
    qreal m_progress = 0.0;
};
} // namespace

M3Dialog::M3Dialog(QWidget *parent) : QDialog(parent), m_frameless(MaterialTheme::materialActive())
{
    setObjectName(QStringLiteral("M3Dialog"));
    if (m_frameless) {
        setAttribute(Qt::WA_TranslucentBackground);
        m_frameHelper = new FramelessHelper(this);
        m_frameHelper->setResizable(false);
        m_frameHelper->setEnabled(true);
        // Any empty part of the card moves the dialog.
        m_frameHelper->addDragArea(this);
    }

    m_content = new DialogCard(this, m_frameless);
    m_content->setObjectName(QStringLiteral("M3DialogContent"));
    m_icon = new QLabel(m_content);
    m_icon->setAlignment(Qt::AlignCenter);
    m_icon->hide();
    m_headline = new QLabel(m_content);
    m_headline->setProperty("m3", QStringLiteral("headline"));
    m_headline->setWordWrap(true);
    m_headline->hide();
    m_supporting = new QLabel(m_content);
    m_supporting->setProperty("m3", QStringLiteral("muted"));
    m_supporting->setWordWrap(true);
    m_supporting->setTextFormat(Qt::AutoText);
    m_supporting->setOpenExternalLinks(true);
    m_supporting->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_supporting->hide();

    m_body = new QVBoxLayout;
    m_body->setSpacing(12);
    m_actions = new QHBoxLayout;
    m_actions->setSpacing(8);
    m_actions->addStretch(1);

    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(24, 24, 24, 20);
    contentLayout->setSpacing(14);
    contentLayout->addWidget(m_icon);
    contentLayout->addWidget(m_headline);
    contentLayout->addWidget(m_supporting);
    contentLayout->addLayout(m_body, 1);
    contentLayout->addSpacing(6);
    contentLayout->addLayout(m_actions);

    auto *outer = new QVBoxLayout(this);
    const int margin = m_frameless ? kShadow : 0;
    outer->setContentsMargins(margin, margin, margin, margin);
    outer->addWidget(m_content);

    setMinimumWidth(m_frameless ? 380 + 2 * kShadow : 380);
}

void M3Dialog::setHeadline(const QString &text, const QString &iconName)
{
    setWindowTitle(text);
    m_headline->setText(text);
    m_headline->setVisible(!text.isEmpty());
    // Material dialogs center the headline under a hero icon.
    m_headline->setAlignment(iconName.isEmpty() ? Qt::AlignLeft : Qt::AlignHCenter);
    if (!iconName.isEmpty()) {
        m_icon->setPixmap(MaterialTheme::icon(iconName, MaterialTheme::current().primary, 26).pixmap(26, 26));
        m_icon->show();
    }
}

void M3Dialog::setSupportingText(const QString &text)
{
    m_supporting->setText(text);
    m_supporting->setVisible(!text.isEmpty());
}

QPushButton *M3Dialog::addAction(const QString &text, const QString &role, const QString &iconName)
{
    auto *button = new QPushButton(text, m_content);
    MaterialTheme::styleButton(button, role, iconName);
    button->setAutoDefault(false);
    if (role == QLatin1String("filled")) {
        button->setDefault(true);
    }
    m_actions->addWidget(button);
    return button;
}

void M3Dialog::setResizable(bool resizable)
{
    if (m_frameHelper)
        m_frameHelper->setResizable(resizable);
}

void M3Dialog::paintEvent(QPaintEvent *event)
{
    if (!m_frameless) {
        QDialog::paintEvent(event);
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow);

    // A soft shadow from stacked translucent outlines, offset slightly down
    // (Material's elevation level 3).
    painter.setPen(Qt::NoPen);
    for (int i = kShadow; i > 0; i -= 2) {
        const qreal t = qreal(i) / kShadow;
        painter.setBrush(QColor(0, 0, 0, int(14 * (1.0 - t) * (1.0 - t) + 1)));
        painter.drawRoundedRect(card.adjusted(-i, -i + 4, i, i + 4), kRadius + i, kRadius + i);
    }
    // The card itself is painted by m_content (DialogCard), so the open
    // animation can scale it.
}

void M3Dialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!Motion::enabled())
        return;
    // Scale and fade the card in (and the whole window, shadow included,
    // where the compositor supports window opacity).
    auto *effect = new ScaleFadeEffect(m_content);
    m_content->setGraphicsEffect(effect);
    auto *animation = new QVariantAnimation(effect);
    animation->setDuration(std::max(1, Motion::duration(220)));
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, effect, [effect](const QVariant &value) { effect->setProgress(value.toDouble()); });
    connect(animation, &QVariantAnimation::finished, this, [this]() {
        // Effects re-render everything offscreen; drop it once it's done.
        m_content->setGraphicsEffect(nullptr);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    auto *windowFade = new QPropertyAnimation(this, "windowOpacity", this);
    windowFade->setDuration(std::max(1, Motion::duration(160)));
    windowFade->setStartValue(0.0);
    windowFade->setEndValue(1.0);
    windowFade->start(QAbstractAnimation::DeleteWhenStopped);
}

namespace M3Message {

namespace {

QString buttonText(QMessageBox::StandardButton button)
{
    switch (button) {
    case QMessageBox::Ok:
        return QObject::tr("OK");
    case QMessageBox::Yes:
        return QObject::tr("Yes");
    case QMessageBox::No:
        return QObject::tr("No");
    case QMessageBox::Cancel:
        return QObject::tr("Cancel");
    case QMessageBox::Close:
        return QObject::tr("Close");
    case QMessageBox::Save:
        return QObject::tr("Save");
    case QMessageBox::Discard:
        return QObject::tr("Discard");
    case QMessageBox::Retry:
        return QObject::tr("Retry");
    case QMessageBox::Ignore:
        return QObject::tr("Ignore");
    case QMessageBox::Abort:
        return QObject::tr("Abort");
    case QMessageBox::Apply:
        return QObject::tr("Apply");
    default:
        return QObject::tr("OK");
    }
}

bool isDismissive(QMessageBox::StandardButton button)
{
    return button == QMessageBox::No || button == QMessageBox::Cancel || button == QMessageBox::Close
        || button == QMessageBox::Ignore || button == QMessageBox::Abort || button == QMessageBox::Discard;
}

QMessageBox::StandardButton show(QMessageBox::Icon kind, QWidget *parent, const QString &title, const QString &text,
                                 QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    if (!MaterialTheme::materialActive()) {
        QMessageBox box(kind, title, text, buttons, parent);
        if (defaultButton != QMessageBox::NoButton)
            box.setDefaultButton(defaultButton);
        return QMessageBox::StandardButton(box.exec());
    }

    const QString icon = kind == QMessageBox::Critical ? QStringLiteral("error")
        : kind == QMessageBox::Warning                 ? QStringLiteral("warning")
        : kind == QMessageBox::Question                ? QStringLiteral("help")
                                                       : QStringLiteral("info");
    M3Dialog dialog(parent);
    dialog.setHeadline(title, icon);
    dialog.setSupportingText(text);

    // Dismissive actions first, the confirming one last (Material order).
    QVector<QMessageBox::StandardButton> order;
    for (int bit = QMessageBox::FirstButton; bit <= QMessageBox::LastButton; bit <<= 1) {
        const auto button = QMessageBox::StandardButton(bit);
        if (buttons.testFlag(button))
            order.append(button);
    }
    std::stable_partition(order.begin(), order.end(), isDismissive);
    if (order.isEmpty())
        order.append(QMessageBox::Ok);

    QMessageBox::StandardButton result = QMessageBox::NoButton;
    QMessageBox::StandardButton escape = order.first();
    for (const QMessageBox::StandardButton button : order) {
        const bool confirming = button == order.last() && !isDismissive(button);
        QPushButton *push = dialog.addAction(buttonText(button), confirming ? QStringLiteral("filled") : QStringLiteral("text"));
        QObject::connect(push, &QPushButton::clicked, &dialog, [&dialog, &result, button]() {
            result = button;
            dialog.accept();
        });
        if (button == defaultButton || (defaultButton == QMessageBox::NoButton && confirming)) {
            push->setDefault(true);
            push->setFocus();
        }
        if (isDismissive(button))
            escape = button;
    }
    dialog.setMaximumWidth(560);
    if (dialog.exec() != QDialog::Accepted || result == QMessageBox::NoButton)
        return escape;
    return result;
}

} // namespace

QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                        QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return show(QMessageBox::Information, parent, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return show(QMessageBox::Warning, parent, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return show(QMessageBox::Critical, parent, title, text, buttons, defaultButton);
}

QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    return show(QMessageBox::Question, parent, title, text, buttons, defaultButton);
}

void about(QWidget *parent, const QString &title, const QString &text)
{
    if (!MaterialTheme::materialActive()) {
        QMessageBox::about(parent, title, text);
        return;
    }
    M3Dialog dialog(parent);
    dialog.setHeadline(QString(), QString());
    dialog.setWindowTitle(title);
    dialog.setSupportingText(text);
    QPushButton *close = dialog.addAction(QObject::tr("Close"), QStringLiteral("filled"));
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.setMaximumWidth(560);
    dialog.exec();
}

} // namespace M3Message
