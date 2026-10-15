#include "M3Widgets.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "../core/SystemTools.h"
#include "MaterialTheme.h"
#include "Motion.h"

Snackbar::Snackbar(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Snackbar"));
    setAttribute(Qt::WA_StyledBackground);
    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_action = new QPushButton(this);
    MaterialTheme::styleButton(m_action, QStringLiteral("text"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 6, 8, 6);
    layout->setSpacing(12);
    layout->addWidget(m_text, 1);
    layout->addWidget(m_action);

    m_opacity = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacity);
    m_slide = new QPropertyAnimation(this, "pos", this);
    m_slide->setEasingCurve(QEasingCurve::OutCubic);
    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    m_animation = new QParallelAnimationGroup(this);
    m_animation->addAnimation(m_slide);
    m_animation->addAnimation(m_fade);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Snackbar::dismiss);
    connect(m_action, &QPushButton::clicked, this, [this]() {
        dismiss();
        if (m_callback)
            m_callback();
    });
    parent->installEventFilter(this);
    hide();
}

void Snackbar::showMessage(const QString &text, const QString &actionText, std::function<void()> action)
{
    m_text->setText(text);
    m_action->setText(actionText);
    m_action->setVisible(!actionText.isEmpty());
    m_callback = std::move(action);
    reposition();
    // Rises from just below its resting place while fading in.
    m_animation->stop();
    disconnect(m_animation, &QAbstractAnimation::finished, this, nullptr);
    const int ms = Motion::duration(220);
    m_slide->setDuration(std::max(1, ms));
    m_fade->setDuration(std::max(1, ms));
    m_slide->setStartValue(restingPosition() + QPoint(0, 24));
    m_slide->setEndValue(restingPosition());
    m_fade->setStartValue(isVisible() ? m_opacity->opacity() : 0.0);
    m_fade->setEndValue(1.0);
    show();
    raise();
    m_animation->start();
    m_timer->start(actionText.isEmpty() ? 4000 : 7000);
}

void Snackbar::dismiss()
{
    m_timer->stop();
    m_animation->stop();
    const int ms = Motion::duration(180);
    m_slide->setDuration(std::max(1, ms));
    m_fade->setDuration(std::max(1, ms));
    m_slide->setStartValue(pos());
    m_slide->setEndValue(restingPosition() + QPoint(0, 24));
    m_fade->setStartValue(m_opacity->opacity());
    m_fade->setEndValue(0.0);
    connect(m_animation, &QAbstractAnimation::finished, this, &QWidget::hide, Qt::UniqueConnection);
    m_animation->start();
}

QPoint Snackbar::restingPosition() const
{
    const QWidget *host = parentWidget();
    return QPoint((host->width() - width()) / 2, host->height() - height() - 24);
}

bool Snackbar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        reposition();
    return QFrame::eventFilter(watched, event);
}

void Snackbar::reposition()
{
    QWidget *host = parentWidget();
    const int width = std::min(560, host->width() - 32);
    m_text->setFixedWidth(width - 32 - (m_action->isVisible() ? m_action->sizeHint().width() + 12 : 0));
    adjustSize();
    resize(width, std::max(48, sizeHint().height()));
    if (m_animation->state() != QAbstractAnimation::Running)
        move(restingPosition());
}

StackedBar::StackedBar(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(12);
}

void StackedBar::setSegments(const QVector<QPair<int, QColor>> &segments)
{
    m_segments = segments;
    update();
}

void StackedBar::paintEvent(QPaintEvent *)
{
    int total = 0;
    int nonEmpty = 0;
    for (const auto &segment : m_segments) {
        total += segment.first;
        nonEmpty += segment.first > 0 ? 1 : 0;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    if (total == 0) {
        QColor track = MaterialTheme::current().onPrimaryContainer;
        track.setAlphaF(0.15f);
        painter.setBrush(track);
        painter.drawRoundedRect(rect(), 6, 6);
        return;
    }
    constexpr int gap = 3;
    const double available = width() - gap * (nonEmpty - 1);
    double x = 0;
    for (const auto &segment : m_segments) {
        if (segment.first <= 0)
            continue;
        const double w = available * segment.first / total;
        painter.setBrush(segment.second);
        painter.drawRoundedRect(QRectF(x, 0, std::max(w, 6.0), height()), 6, 6);
        x += w + gap;
    }
}

M3Switch::M3Switch(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(52, 32);
    m_position.setStartValue(0.0);
    m_position.setEndValue(0.0);
    m_position.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_position, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

void M3Switch::setCheckedQuietly(bool checked)
{
    const QSignalBlocker blocker(this);
    setChecked(checked);
    m_position.stop();
    m_position.setStartValue(checked ? 1.0 : 0.0);
    m_position.setEndValue(checked ? 1.0 : 0.0);
    m_position.start();
    m_position.setCurrentTime(m_position.duration());
    update();
}

void M3Switch::checkStateSet()
{
    QAbstractButton::checkStateSet();
    const qreal from = m_position.currentValue().isValid() ? m_position.currentValue().toDouble() : 0.0;
    m_position.stop();
    m_position.setStartValue(from);
    m_position.setEndValue(isChecked() ? 1.0 : 0.0);
    m_position.setDuration(std::max(1, Motion::duration(180)));
    m_position.start();
}

void M3Switch::nextCheckState()
{
    QAbstractButton::nextCheckState();
    checkStateSet();
}

void M3Switch::paintEvent(QPaintEvent *)
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    const qreal t = m_position.currentValue().isValid() ? m_position.currentValue().toDouble() : (isChecked() ? 1.0 : 0.0);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(isEnabled() ? 1.0 : 0.38);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    // Track: outlined surface when off, primary when on.
    painter.setPen(t < 0.5 ? QPen(s.outline, 2) : Qt::NoPen);
    painter.setBrush(t < 0.5 ? s.surfaceContainerHighest : s.primary);
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    // Thumb: small and outline-colored off, larger and on-primary on.
    const qreal diameter = 16 + 8 * t;
    const qreal centerX = 16 + (width() - 32) * t;
    const QRectF thumb(centerX - diameter / 2, (height() - diameter) / 2, diameter, diameter);
    painter.setPen(Qt::NoPen);
    painter.setBrush(t < 0.5 ? s.outline : s.onPrimary);
    painter.drawEllipse(thumb);
    if (hasFocus()) {
        painter.setPen(QPen(s.primary, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(-1, -1, 1, 1), track.height() / 2 + 1, track.height() / 2 + 1);
    }
}

namespace M3 {

QWidget *activityRow(const SystemTools::Activity &a, QWidget *parent)
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    const QString icon = a.action == QLatin1String("installed") ? QStringLiteral("add")
        : a.action == QLatin1String("removed")                  ? QStringLiteral("delete")
        : a.action == QLatin1String("reinstalled")              ? QStringLiteral("restart_alt")
        : a.action == QLatin1String("downgraded")               ? QStringLiteral("history")
                                                                : QStringLiteral("update");
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(12);
    auto *iconLabel = new QLabel(row);
    QPixmap tile(QSize(36, 36) * 2);
    tile.setDevicePixelRatio(2);
    tile.fill(Qt::transparent);
    {
        QPainter painter(&tile);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(s.secondaryContainer);
        painter.drawRoundedRect(QRect(0, 0, 36, 36), 10, 10);
        painter.setPen(s.onSecondaryContainer);
        painter.setFont(MaterialTheme::iconFont(18));
        painter.drawText(QRect(0, 0, 36, 36), Qt::AlignCenter, icon);
    }
    iconLabel->setPixmap(tile);
    QString verb = a.action;
    if (!verb.isEmpty())
        verb[0] = verb[0].toUpper();
    auto *text = new QLabel(QStringLiteral("<b>%1</b> %2 <span style=\"color:%3\">%4</span>")
                                .arg(verb.toHtmlEscaped(), a.package.toHtmlEscaped(), s.onSurfaceVariant.name(), a.detail.toHtmlEscaped()),
                            row);
    text->setTextFormat(Qt::RichText);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QString when;
    if (a.time.isValid()) {
        const qint64 secs = a.time.secsTo(QDateTime::currentDateTime());
        when = secs < 60      ? QObject::tr("just now")
            : secs < 3600     ? QObject::tr("%n min ago", "", int(secs / 60))
            : secs < 86400    ? QObject::tr("%n h ago", "", int(secs / 3600))
            : secs < 2 * 86400 ? QObject::tr("yesterday")
                              : QLocale().toString(a.time, QLocale::ShortFormat);
    }
    auto *meta = label(QStringLiteral("%1 · %2").arg(a.sourceKey, when), QStringLiteral("muted"), row);
    layout->addWidget(iconLabel);
    layout->addWidget(text, 1);
    layout->addWidget(meta);
    return row;
}

QLabel *label(const QString &text, const QString &role, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setProperty("m3", role);
    return l;
}

QButtonGroup *chipRow(const QVector<QPair<QString, QString>> &labelsAndIcons, QWidget *parent, QHBoxLayout *into)
{
    auto *group = new QButtonGroup(parent);
    group->setExclusive(true);
    for (int i = 0; i < labelsAndIcons.size(); ++i) {
        auto *chip = new QPushButton(labelsAndIcons[i].first, parent);
        chip->setCheckable(true);
        MaterialTheme::styleButton(chip, QStringLiteral("chip"), labelsAndIcons[i].second);
        group->addButton(chip, i);
        into->addWidget(chip);
    }
    if (QAbstractButton *first = group->button(0))
        first->setChecked(true);
    return group;
}

QButtonGroup *segmented(const QVector<QPair<QString, QString>> &labelsAndIcons, QWidget *parent, QHBoxLayout *into)
{
    auto *group = new QButtonGroup(parent);
    group->setExclusive(true);
    auto *row = new QHBoxLayout;
    row->setSpacing(0);
    const int count = labelsAndIcons.size();
    for (int i = 0; i < count; ++i) {
        auto *button = new QPushButton(labelsAndIcons[i].first, parent);
        button->setCheckable(true);
        button->setProperty("m3pos", count == 1 ? "only" : i == 0 ? "first" : i == count - 1 ? "last" : "middle");
        MaterialTheme::styleButton(button, QStringLiteral("segment"), labelsAndIcons[i].second);
        group->addButton(button, i);
        row->addWidget(button);
    }
    if (QAbstractButton *first = group->button(0))
        first->setChecked(true);
    into->addLayout(row);
    return group;
}

// Seed swatches plus a custom-hue slider for the Material You accent. Each
// swatch previews its scheme's primary and container colors.
QWidget *accentPicker(QWidget *parent)
{
    static const QVector<QPair<QString, int>> seeds = {{QObject::tr("Olive"), 128}, {QObject::tr("Teal"), 195},
                                                       {QObject::tr("Indigo"), 268}, {QObject::tr("Rose"), 355},
                                                       {QObject::tr("Amber"), 72},  {QObject::tr("Slate"), 245}};
    auto *picker = new QWidget(parent);
    auto *layout = new QVBoxLayout(picker);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto *swatchRow = new QHBoxLayout;
    swatchRow->setSpacing(10);
    QVector<QPushButton *> swatches;
    auto *slider = new QSlider(Qt::Horizontal, picker);
    slider->setRange(0, 359);
    slider->setValue(AppSettings::instance().seedHue());
    slider->setToolTip(QObject::tr("Custom hue"));

    for (const auto &seed : seeds) {
        auto *swatch = new QPushButton(picker);
        swatch->setFixedSize(40, 40);
        swatch->setProperty("hue", seed.second);
        swatch->setToolTip(seed.first);
        swatch->setAccessibleName(seed.first);
        swatch->setCursor(Qt::PointingHandCursor);
        swatchRow->addWidget(swatch);
        swatches.append(swatch);
    }
    swatchRow->addStretch(1);
    // Owned by the picker so the lambdas below can share it.
    auto *owned = new QVector<QPushButton *>(swatches);
    QObject::connect(picker, &QObject::destroyed, [owned]() { delete owned; });
    auto apply = [owned]() {
        const int hue = AppSettings::instance().seedHue();
        for (QPushButton *swatch : *owned) {
            const int h = swatch->property("hue").toInt();
            const QColor primary = MaterialTheme::oklch(0.55, 0.13, h);
            const QColor container = MaterialTheme::oklch(0.85, 0.07, h);
            swatch->setStyleSheet(QStringLiteral("QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %1, "
                                                 "stop:0.5 %1, stop:0.51 %2, stop:1 %2); border-radius: 14px; border: %3; "
                                                 "min-width: 40px; max-width: 40px; min-height: 40px; max-height: 40px; padding: 0; }")
                                      .arg(primary.name(), container.name(),
                                           h == hue ? QStringLiteral("3px solid %1").arg(MaterialTheme::current().onSurface.name())
                                                    : QStringLiteral("none")));
        }
    };
    apply();
    for (QPushButton *swatch : swatches) {
        QObject::connect(swatch, &QPushButton::clicked, picker, [swatch, slider]() {
            const QSignalBlocker blocker(slider);
            slider->setValue(swatch->property("hue").toInt());
            AppSettings::instance().setSeedHue(swatch->property("hue").toInt());
        });
    }
    // Re-generating the whole theme on every slider step is too heavy; apply
    // when the handle is released (or after keyboard steps settle).
    QObject::connect(slider, &QSlider::sliderReleased, picker, [slider]() { AppSettings::instance().setSeedHue(slider->value()); });
    QObject::connect(slider, &QSlider::valueChanged, picker, [slider](int value) {
        if (!slider->isSliderDown())
            AppSettings::instance().setSeedHue(value);
    });
    QObject::connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, picker, apply);

    layout->addLayout(swatchRow);
    auto *sliderRow = new QHBoxLayout;
    sliderRow->addWidget(new QLabel(QObject::tr("Custom hue"), picker));
    sliderRow->addWidget(slider, 1);
    layout->addLayout(sliderRow);
    return picker;
}

QFrame *card(const QString &role, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setProperty("m3", role);
    return frame;
}

QLabel *symbolLabel(const QString &symbol, int size, QWidget *parent, bool tile)
{
    auto *label = new QLabel(parent);
    const int box = tile ? 40 : size;
    label->setFixedSize(box, box);
    label->setAlignment(Qt::AlignCenter);
    auto paint = [label, symbol, size, tile]() {
        const MaterialTheme::Scheme &s = MaterialTheme::current();
        label->setPixmap(MaterialTheme::icon(symbol, tile ? s.onSecondaryContainer : s.primary, size).pixmap(size, size));
        label->setStyleSheet(tile ? QStringLiteral("background: %1; border-radius: 12px;").arg(s.secondaryContainer.name())
                                  : QString());
    };
    paint();
    QObject::connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, label, paint);
    return label;
}

QFrame *section(QWidget *parent, const QString &icon, const QString &title, const QString &text, QVBoxLayout *&content)
{
    QFrame *frame = card(QStringLiteral("card"), parent);
    content = new QVBoxLayout(frame);
    content->setContentsMargins(20, 18, 20, 18);
    content->setSpacing(10);
    auto *head = new QHBoxLayout;
    head->setSpacing(12);
    head->addWidget(symbolLabel(icon, 22, frame, false));
    head->addWidget(label(title, QStringLiteral("titleM"), frame), 1);
    content->addLayout(head);
    if (!text.isEmpty()) {
        auto *body = label(text, QStringLiteral("muted"), frame);
        body->setWordWrap(true);
        content->addWidget(body);
    }
    return frame;
}

namespace {

// Clicking anywhere on a switch row flips its switch, as in M3 lists.
class SwitchRow : public QWidget {
public:
    SwitchRow(QWidget *parent, M3Switch *toggle) : QWidget(parent), m_toggle(toggle)
    {
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && m_toggle->isEnabled())
            m_toggle->click();
        QWidget::mouseReleaseEvent(event);
    }

private:
    M3Switch *m_toggle;
};

} // namespace

QWidget *switchRow(QWidget *parent, const QString &symbol, const QString &title, const QString &supporting, M3Switch *&toggle)
{
    toggle = new M3Switch(nullptr);
    auto *row = new SwitchRow(parent, toggle);
    toggle->setParent(row);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 8, 0, 8);
    layout->setSpacing(16);
    if (!symbol.isEmpty())
        layout->addWidget(symbolLabel(symbol, 22, row, true), 0, Qt::AlignVCenter);
    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *titleLabel = label(title, QString(), row);
    titleLabel->setWordWrap(true);
    text->addWidget(titleLabel);
    if (!supporting.isEmpty()) {
        auto *support = label(supporting, QStringLiteral("muted"), row);
        support->setWordWrap(true);
        text->addWidget(support);
    }
    layout->addLayout(text, 1);
    layout->addWidget(toggle, 0, Qt::AlignVCenter);
    return row;
}

QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFixedHeight(1);
    auto paint = [line]() { line->setStyleSheet(QStringLiteral("background: %1;").arg(MaterialTheme::current().outlineVariant.name())); };
    paint();
    QObject::connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, line, paint);
    return line;
}

} // namespace M3
