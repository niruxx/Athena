#include "Motion.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QRadioButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVariantAnimation>

#include "../core/AppSettings.h"
#include "MaterialTheme.h"

namespace Motion {

namespace {

// The desktop's own animation speed, read once: KDE scales every
// animation by AnimationDurationFactor (0 = off), GNOME has an on/off key.
double desktopFactor()
{
    static const double factor = []() {
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
        if (desktop.contains(QLatin1String("GNOME"), Qt::CaseInsensitive)) {
            QProcess gsettings;
            gsettings.start(QStringLiteral("gsettings"),
                            {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"), QStringLiteral("enable-animations")});
            if (gsettings.waitForFinished(1500) && gsettings.readAllStandardOutput().trimmed() == "false")
                return 0.0;
        }
        const QSettings kde(QDir::homePath() + QStringLiteral("/.config/kdeglobals"), QSettings::IniFormat);
        return std::clamp(kde.value(QStringLiteral("KDE/AnimationDurationFactor"), 1.0).toDouble(), 0.0, 4.0);
    }();
    return factor;
}

QColor contentColor(const QString &role)
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    if (role == QLatin1String("filled"))
        return s.onPrimary;
    if (role == QLatin1String("tonal"))
        return s.onSecondaryContainer;
    if (role == QLatin1String("danger") || role == QLatin1String("windowClose"))
        return s.onErrorContainer;
    if (role == QLatin1String("chip") || role == QLatin1String("segment") || role == QLatin1String("icon")
        || role == QLatin1String("window"))
        return s.onSurface;
    return s.primary; // outlined, text, and plain push buttons
}

// The same outline the stylesheet gives each role, so the tint and ripple
// stay inside the button's visible shape.
QPainterPath shapeFor(const QAbstractButton *button)
{
    const QRectF r = button->rect();
    const QString role = button->property("m3").toString();
    const qreal pill = r.height() / 2.0;
    QPainterPath path;
    if (role == QLatin1String("chip")) {
        path.addRoundedRect(r, 8, 8);
        return path;
    }
    if (role == QLatin1String("segment")) {
        const QString pos = button->property("m3pos").toString();
        const qreal left = (pos == QLatin1String("first") || pos == QLatin1String("only")) ? pill : 0;
        const qreal right = (pos == QLatin1String("last") || pos == QLatin1String("only")) ? pill : 0;
        path.moveTo(r.left() + left, r.top());
        path.lineTo(r.right() - right, r.top());
        if (right > 0)
            path.arcTo(QRectF(r.right() - 2 * right, r.top(), 2 * right, r.height()), 90, -180);
        else {
            path.lineTo(r.right(), r.top());
            path.lineTo(r.right(), r.bottom());
        }
        path.lineTo(r.left() + left, r.bottom());
        if (left > 0)
            path.arcTo(QRectF(r.left(), r.top(), 2 * left, r.height()), 270, -180);
        else {
            path.lineTo(r.left(), r.bottom());
            path.lineTo(r.left(), r.top());
        }
        path.closeSubpath();
        return path;
    }
    const qreal radius = role == QLatin1String("window") || role == QLatin1String("windowClose") ? 16 : pill;
    path.addRoundedRect(r, std::min(radius, pill), std::min(radius, pill));
    return path;
}

// Material's state layer: a hover tint and a press ripple drawn over a
// button in its content color. Transparent to input; lives as the
// button's top-most child.
class StateLayer : public QWidget {
public:
    explicit StateLayer(QAbstractButton *button) : QWidget(button), m_button(button)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        resize(button->size());
        button->installEventFilter(this);

        m_hover.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_hover, &QVariantAnimation::valueChanged, this, [this]() { update(); });
        m_ripple.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_ripple, &QVariantAnimation::valueChanged, this, [this]() { update(); });
        m_rippleFade.setEasingCurve(QEasingCurve::Linear);
        connect(&m_rippleFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_rippleOpacity = value.toDouble();
            update();
        });
        raise();
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Resize:
            resize(m_button->size());
            break;
        case QEvent::Show:
        case QEvent::ChildAdded:
            raise();
            break;
        case QEvent::Enter:
            animate(m_hover, 1.0, 150);
            break;
        case QEvent::Leave:
            animate(m_hover, 0.0, 150);
            break;
        case QEvent::MouseButtonPress: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            m_center = mouse->position();
            const qreal reach = std::hypot(std::max(m_center.x(), width() - m_center.x()), std::max(m_center.y(), height() - m_center.y()));
            m_rippleFade.stop();
            m_rippleOpacity = 1.0;
            m_rippleFade.setStartValue(1.0);
            m_ripple.stop();
            m_ripple.setStartValue(0.0);
            m_ripple.setEndValue(reach);
            m_ripple.setDuration(std::max(1, duration(380)));
            m_ripple.start();
            if (!enabled())
                m_ripple.setCurrentTime(m_ripple.duration());
            break;
        }
        case QEvent::MouseButtonRelease:
        case QEvent::FocusOut:
            m_rippleFade.stop();
            m_rippleFade.setStartValue(m_rippleOpacity);
            m_rippleFade.setEndValue(0.0);
            m_rippleFade.setDuration(std::max(1, duration(300)));
            m_rippleFade.start();
            break;
        default:
            break;
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        if (!MaterialTheme::materialActive() || !m_button->isEnabled())
            return;
        const qreal hover = m_hover.currentValue().isValid() ? m_hover.currentValue().toDouble() : 0.0;
        const qreal radius = m_ripple.currentValue().isValid() ? m_ripple.currentValue().toDouble() : 0.0;
        const bool rippling = radius > 0 && m_rippleOpacity > 0.01;
        if (hover < 0.01 && !rippling)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipPath(shapeFor(m_button));
        QColor color = contentColor(m_button->property("m3").toString());
        if (hover > 0.01) {
            color.setAlphaF(float(0.08 * hover));
            painter.fillRect(rect(), color);
        }
        if (rippling) {
            color.setAlphaF(float(0.12 * m_rippleOpacity));
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(m_center, radius, radius);
        }
    }

private:
    void animate(QVariantAnimation &animation, qreal to, int ms)
    {
        const QVariant from = animation.currentValue().isValid() ? animation.currentValue() : QVariant(0.0);
        animation.stop();
        animation.setStartValue(from);
        animation.setEndValue(to);
        animation.setDuration(std::max(1, duration(ms)));
        animation.start();
        if (!enabled())
            animation.setCurrentTime(animation.duration());
    }

    QAbstractButton *m_button;
    QVariantAnimation m_hover;
    QVariantAnimation m_ripple;
    QVariantAnimation m_rippleFade;
    QPointF m_center;
    qreal m_rippleOpacity = 0.0;
};

// Paints a snapshot fading out (and drifting down a few pixels) over the
// live content beneath it, then deletes itself.
class FadeOverlay : public QWidget {
public:
    FadeOverlay(QWidget *host, const QRect &rect, const QPixmap &before) : QWidget(host), m_before(before)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(rect);
        m_animation.setStartValue(1.0);
        m_animation.setEndValue(0.0);
        m_animation.setDuration(std::max(1, duration(220)));
        m_animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_animation, &QVariantAnimation::valueChanged, this, [this]() { update(); });
        connect(&m_animation, &QVariantAnimation::finished, this, &QObject::deleteLater);
        show();
        raise();
        m_animation.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const qreal opacity = m_animation.currentValue().toDouble();
        QPainter painter(this);
        painter.setOpacity(opacity);
        painter.drawPixmap(QPointF(0, (1.0 - opacity) * 10.0), m_before);
    }

private:
    QPixmap m_before;
    QVariantAnimation m_animation;
};

class HoverTracker : public QObject {
public:
    explicit HoverTracker(QAbstractItemView *view) : QObject(view), m_view(view)
    {
        view->viewport()->installEventFilter(this);
        view->setProperty("hoverTracker", QVariant::fromValue<QObject *>(this));
        m_timer.setInterval(16);
        connect(&m_timer, &QTimer::timeout, this, &HoverTracker::step);
    }

    qreal progress(int row) const { return m_progress.value(row, 0.0); }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove) {
            const QPoint pos = event->type() == QEvent::MouseMove ? static_cast<QMouseEvent *>(event)->position().toPoint()
                                                                  : static_cast<QHoverEvent *>(event)->position().toPoint();
            setHovered(m_view->indexAt(pos).row());
        } else if (event->type() == QEvent::Leave) {
            setHovered(-1);
        }
        return false;
    }

private:
    void setHovered(int row)
    {
        if (row == m_hovered)
            return;
        m_hovered = row;
        if (!enabled()) {
            m_progress.clear();
            if (row >= 0)
                m_progress.insert(row, 1.0);
            m_view->viewport()->update();
            return;
        }
        if (row >= 0 && !m_progress.contains(row))
            m_progress.insert(row, 0.0);
        m_clock.restart();
        m_timer.start();
    }

    void step()
    {
        const qreal delta = m_clock.restart() / qreal(std::max(1, duration(160)));
        bool active = false;
        for (auto it = m_progress.begin(); it != m_progress.end();) {
            const qreal target = it.key() == m_hovered ? 1.0 : 0.0;
            qreal &value = it.value();
            value = target > value ? std::min(target, value + delta) : std::max(target, value - delta);
            if (m_view->model())
                m_view->viewport()->update(m_view->visualRect(m_view->model()->index(it.key(), 0)));
            if (value != target)
                active = true;
            if (value == 0.0 && target == 0.0)
                it = m_progress.erase(it);
            else
                ++it;
        }
        if (!active)
            m_timer.stop();
    }

    QAbstractItemView *m_view;
    QHash<int, qreal> m_progress;
    int m_hovered = -1;
    QTimer m_timer;
    QElapsedTimer m_clock;
};

void attachTabTransitions(QTabWidget *tabs)
{
    // The old page has to be captured before the switch; tabBarClicked
    // fires first, currentChanged after.
    auto snapshot = std::make_shared<QPixmap>();
    QObject::connect(tabs, &QTabWidget::tabBarClicked, tabs, [tabs, snapshot](int index) {
        if (enabled() && index != tabs->currentIndex() && tabs->currentWidget())
            *snapshot = tabs->currentWidget()->grab();
    });
    QObject::connect(tabs, &QTabWidget::currentChanged, tabs, [tabs, snapshot]() {
        if (snapshot->isNull() || !tabs->currentWidget())
            return;
        QWidget *page = tabs->currentWidget();
        fadeOver(tabs, QRect(page->mapTo(tabs, QPoint(0, 0)), page->size()), *snapshot);
        *snapshot = QPixmap();
    });
}

class AppHooks : public QObject {
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Polish)
            return false;
        if (auto *button = qobject_cast<QAbstractButton *>(watched)) {
            const bool skip = qobject_cast<QCheckBox *>(button) || qobject_cast<QRadioButton *>(button)
                || qobject_cast<QLineEdit *>(button->parentWidget()) || qobject_cast<QTabBar *>(button->parentWidget())
                || button->inherits("RailButton") || button->property("m3layer").toBool();
            if (!skip) {
                button->setProperty("m3layer", true);
                new StateLayer(button);
            }
        } else if (auto *tabs = qobject_cast<QTabWidget *>(watched)) {
            if (!tabs->property("m3tabs").toBool()) {
                tabs->setProperty("m3tabs", true);
                attachTabTransitions(tabs);
            }
        }
        return false;
    }
};

} // namespace

bool enabled()
{
    return AppSettings::instance().animationsEnabled() && desktopFactor() > 0.0;
}

int duration(int ms)
{
    return enabled() ? int(ms * desktopFactor()) : 0;
}

void install(QApplication *app)
{
    app->installEventFilter(new AppHooks());
}

void switchStack(QStackedWidget *stack, int index)
{
    if (index == stack->currentIndex())
        return;
    if (!enabled() || !stack->currentWidget() || !stack->isVisible()) {
        stack->setCurrentIndex(index);
        return;
    }
    const QPixmap before = stack->currentWidget()->grab();
    stack->setCurrentIndex(index);
    fadeOver(stack, stack->rect(), before);
}

void fadeOver(QWidget *host, const QRect &rect, const QPixmap &before)
{
    if (!enabled() || before.isNull())
        return;
    // One transition at a time per host: a new one replaces any in flight.
    if (auto *existing = host->findChild<QWidget *>(QStringLiteral("MotionFadeOverlay"), Qt::FindDirectChildrenOnly))
        delete existing;
    auto *overlay = new FadeOverlay(host, rect, before);
    overlay->setObjectName(QStringLiteral("MotionFadeOverlay"));
}

void crossfadeOnReset(QAbstractItemView *view)
{
    QAbstractItemModel *model = view->model();
    if (!model)
        return;
    auto snapshot = std::make_shared<QPixmap>();
    QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, view, [view, snapshot]() {
        if (enabled() && view->isVisible())
            *snapshot = view->viewport()->grab();
    });
    QObject::connect(model, &QAbstractItemModel::modelReset, view, [view, snapshot]() {
        if (snapshot->isNull())
            return;
        fadeOver(view->viewport(), view->viewport()->rect(), *snapshot);
        *snapshot = QPixmap();
    });
}

void trackHover(QAbstractItemView *view)
{
    new HoverTracker(view);
}

qreal hoverProgress(const QWidget *view, int row, bool hoveredNow)
{
    if (view) {
        if (auto *tracker = dynamic_cast<HoverTracker *>(view->property("hoverTracker").value<QObject *>()))
            return tracker->progress(row);
    }
    return hoveredNow ? 1.0 : 0.0;
}

} // namespace Motion
