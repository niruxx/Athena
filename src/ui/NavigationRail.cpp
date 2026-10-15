#include "NavigationRail.h"

#include <QButtonGroup>
#include <QEnterEvent>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

#include "MaterialTheme.h"
#include "Motion.h"

RailButton::RailButton(const QString &iconName, const QString &label, QWidget *parent)
    : QAbstractButton(parent), m_iconName(iconName)
{
    setText(label);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(label);
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

void RailButton::setBadge(int count)
{
    m_badge = count;
    setAccessibleDescription(count > 0 ? tr("%n pending", "", count) : QString());
    update();
}

QSize RailButton::sizeHint() const
{
    return QSize(80, 60);
}

void RailButton::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void RailButton::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void RailButton::paintEvent(QPaintEvent *)
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect pill = pillRect(width());
    if (!isChecked() && (m_hovered || hasFocus())) {
        QColor hover = s.onSurface;
        hover.setAlphaF(hasFocus() ? 0.12f : 0.08f);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hover);
        painter.drawRoundedRect(pill, 16, 16);
    }
    if (hasFocus()) {
        painter.setPen(QPen(s.primary, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(pill.adjusted(-2, -2, 2, 2), 18, 18);
    }

    painter.setPen(isChecked() ? s.onSecondaryContainer : s.onSurfaceVariant);
    painter.setFont(MaterialTheme::iconFont(24, isChecked()));
    painter.drawText(pill, Qt::AlignCenter, m_iconName);

    if (m_badge > 0) {
        QFont badgeFont = font();
        badgeFont.setPixelSize(11);
        badgeFont.setWeight(QFont::DemiBold);
        const QString text = m_badge > 99 ? QStringLiteral("99+") : QString::number(m_badge);
        const int w = std::max(16, QFontMetrics(badgeFont).horizontalAdvance(text) + 8);
        const QRect badge(pill.center().x() + 4, pill.top() - 2, w, 16);
        painter.setPen(Qt::NoPen);
        painter.setBrush(s.error);
        painter.drawRoundedRect(badge, 8, 8);
        painter.setPen(s.dark ? s.surface : QColor(Qt::white));
        painter.setFont(badgeFont);
        painter.drawText(badge, Qt::AlignCenter, text);
    }

    QFont labelFont = font();
    labelFont.setPixelSize(12);
    labelFont.setWeight(isChecked() ? QFont::Bold : QFont::DemiBold);
    painter.setFont(labelFont);
    painter.setPen(isChecked() ? s.onSurface : s.onSurfaceVariant);
    painter.drawText(QRect(0, pill.bottom() + 4, width(), 18), Qt::AlignHCenter | Qt::AlignTop, text());
}

NavigationRail::NavigationRail(QWidget *parent, Qt::Orientation orientation) : QWidget(parent), m_orientation(orientation)
{
    setObjectName(orientation == Qt::Vertical ? QStringLiteral("NavRail") : QStringLiteral("NavBar"));
    setAttribute(Qt::WA_StyledBackground);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    if (orientation == Qt::Horizontal) {
        setFixedHeight(76);
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(8, 8, 8, 8);
        m_layout->setSpacing(0);
        connect(m_group, &QButtonGroup::idClicked, this, &NavigationRail::currentChanged);
        connect(m_group, &QButtonGroup::idToggled, this, [this](int, bool checked) {
            if (checked)
                moveIndicator(true);
        });
        m_indicatorAnimation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_indicatorAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_indicator = value.toRectF();
            update();
        });
        connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, qOverload<>(&QWidget::update));
        return;
    }

    setFixedWidth(88);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 16, 4, 20);
    // Tight enough for eight destinations in a short window.
    m_layout->setSpacing(4);

    // The app mark at the top of the rail, as in the M3 rail "FAB/menu" slot.
    auto *logo = new QLabel(this);
    logo->setFixedSize(56, 56);
    logo->setAlignment(Qt::AlignCenter);
    auto paintLogo = [logo]() {
        const MaterialTheme::Scheme &s = MaterialTheme::current();
        QPixmap pixmap(QSize(56, 56) * 2);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(s.primaryContainer);
        painter.drawRoundedRect(QRect(0, 0, 56, 56), 16, 16);
        painter.setPen(s.onPrimaryContainer);
        painter.setFont(MaterialTheme::iconFont(28, true));
        painter.drawText(QRect(0, 0, 56, 56), Qt::AlignCenter, QStringLiteral("deployed_code"));
        painter.end();
        logo->setPixmap(pixmap);
    };
    paintLogo();
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, logo, paintLogo);
    logo->setToolTip(tr("Athena"));
    m_layout->addWidget(logo, 0, Qt::AlignHCenter);
    m_layout->addSpacing(12);

    connect(m_group, &QButtonGroup::idClicked, this, &NavigationRail::currentChanged);
    connect(m_group, &QButtonGroup::idToggled, this, [this](int, bool checked) {
        if (checked)
            moveIndicator(true);
    });

    m_indicatorAnimation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_indicatorAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_indicator = value.toRectF();
        update();
    });
    connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, qOverload<>(&QWidget::update));
}

QRectF NavigationRail::targetIndicator() const
{
    const QAbstractButton *checked = m_group->checkedButton();
    if (!checked)
        return {};
    return QRectF(RailButton::pillRect(checked->width()).translated(checked->pos()));
}

void NavigationRail::moveIndicator(bool animate)
{
    const QRectF target = targetIndicator();
    m_indicatorAnimation.stop();
    if (!animate || m_indicator.isNull() || !isVisible() || !Motion::enabled()) {
        m_indicator = target;
        update();
        return;
    }
    m_indicatorAnimation.setStartValue(m_indicator);
    m_indicatorAnimation.setEndValue(target);
    m_indicatorAnimation.setDuration(std::max(1, Motion::duration(300)));
    m_indicatorAnimation.start();
}

void NavigationRail::paintEvent(QPaintEvent *)
{
    if (m_indicator.isNull())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(MaterialTheme::current().secondaryContainer);
    painter.drawRoundedRect(m_indicator, 16, 16);
}

void NavigationRail::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    moveIndicator(false);
}

void NavigationRail::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Buttons only have their final positions once the layout has run.
    QTimer::singleShot(0, this, [this]() { moveIndicator(false); });
}

int NavigationRail::addDestination(const QString &iconName, const QString &label)
{
    auto *button = new RailButton(iconName, label, this);
    const int index = m_buttons.size();
    m_buttons.append(button);
    m_group->addButton(button, index);
    if (m_orientation == Qt::Horizontal)
        m_layout->addWidget(button, 1);
    else
        m_layout->addWidget(button, 0, Qt::AlignHCenter);
    return index;
}

void NavigationRail::addStretch()
{
    if (m_orientation == Qt::Vertical)
        m_layout->addStretch(1);
}

void NavigationRail::clearSelection()
{
    m_group->setExclusive(false);
    for (RailButton *button : m_buttons)
        button->setChecked(false);
    m_group->setExclusive(true);
    m_indicator = QRectF();
    update();
}

void NavigationRail::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_buttons.size())
        return;
    m_buttons[index]->setChecked(true);
}

int NavigationRail::currentIndex() const
{
    return m_group->checkedId();
}

void NavigationRail::setBadge(int index, int count)
{
    if (index >= 0 && index < m_buttons.size())
        m_buttons[index]->setBadge(count);
}
