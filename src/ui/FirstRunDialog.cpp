#include "FirstRunDialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "../core/AppSettings.h"
#include "../core/DistroSupport.h"
#include "../core/ProcessRunner.h"
#include "AurHelperInstallWidget.h"
#include "M3Widgets.h"
#include "MaterialTheme.h"
#include "Motion.h"
#include "PackageFormatInstallWidget.h"

// Wizard progress: a dot per page, the current one stretched into a pill.
class PageDots : public QWidget {
public:
    PageDots(int count, QWidget *parent) : QWidget(parent), m_count(count)
    {
        setFixedSize(count * 14 + 14, 10);
        connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, qOverload<>(&QWidget::update));
    }
    void setCurrent(int index)
    {
        m_current = index;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const MaterialTheme::Scheme &s = MaterialTheme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        int x = 0;
        for (int i = 0; i < m_count; ++i) {
            const int w = i == m_current ? 22 : 8;
            painter.setBrush(i == m_current ? s.primary : s.outlineVariant);
            painter.drawRoundedRect(QRect(x, 1, w, 8), 4, 4);
            x += w + 6;
        }
    }

private:
    int m_count;
    int m_current = 0;
};

namespace {

// A large selectable card: icon, title, and a line of description, outlined
// in the primary color when chosen.
class OptionCard : public QAbstractButton {
public:
    OptionCard(const QString &icon, const QString &title, const QString &description, QWidget *parent)
        : QAbstractButton(parent), m_icon(icon), m_description(description)
    {
        setText(title);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(148);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(title);
        setAccessibleDescription(description);
        connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, this, qOverload<>(&QWidget::update));
    }
    QSize sizeHint() const override { return QSize(240, 148); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const MaterialTheme::Scheme &s = MaterialTheme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        painter.setPen(QPen(isChecked() ? s.primary : s.outlineVariant, isChecked() ? 2.5 : 1));
        painter.setBrush(isChecked() ? s.secondaryContainer : s.surface);
        painter.drawRoundedRect(card, 16, 16);
        if (hasFocus()) {
            painter.setPen(QPen(s.primary, 2, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(card.adjusted(4, 4, -4, -4), 12, 12);
        }

        const QRect iconTile(18, 18, 44, 44);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? s.primary : s.primaryContainer);
        painter.drawRoundedRect(iconTile, 12, 12);
        painter.setPen(isChecked() ? s.onPrimary : s.onPrimaryContainer);
        painter.setFont(MaterialTheme::iconFont(24, true));
        painter.drawText(iconTile, Qt::AlignCenter, m_icon);
        if (isChecked()) {
            painter.setPen(s.primary);
            painter.setFont(MaterialTheme::iconFont(22, true));
            painter.drawText(QRect(width() - 40, 16, 24, 24), Qt::AlignCenter, QStringLiteral("check_circle"));
        }

        QFont titleFont = font();
        titleFont.setPixelSize(16);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(isChecked() ? s.onSecondaryContainer : s.onSurface);
        painter.drawText(QRect(18, 72, width() - 36, 24), Qt::AlignLeft | Qt::AlignVCenter, text());
        QFont bodyFont = font();
        bodyFont.setPixelSize(13);
        painter.setFont(bodyFont);
        painter.setPen(s.onSurfaceVariant);
        painter.drawText(QRect(18, 98, width() - 36, height() - 104), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_description);
    }

private:
    QString m_icon;
    QString m_description;
};

QPixmap appMark(int size)
{
    const MaterialTheme::Scheme &s = MaterialTheme::current();
    QPixmap pixmap(QSize(size, size) * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(s.primaryContainer);
    painter.drawRoundedRect(QRect(0, 0, size, size), size * 0.3, size * 0.3);
    painter.setPen(s.onPrimaryContainer);
    painter.setFont(MaterialTheme::iconFont(int(size * 0.55), true));
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QStringLiteral("deployed_code"));
    return pixmap;
}

// A wizard page: a step label, a headline, a line of supporting text, then
// the page's own content.
QWidget *page(const QString &step, const QString &headline, const QString &supporting, QVBoxLayout *&content)
{
    auto *widget = new QWidget;
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    if (!step.isEmpty())
        layout->addWidget(M3::label(step, QStringLiteral("label"), widget));
    auto *title = M3::label(headline, QStringLiteral("headline"), widget);
    title->setWordWrap(true);
    layout->addWidget(title);
    auto *text = M3::label(supporting, QStringLiteral("muted"), widget);
    text->setWordWrap(true);
    layout->addWidget(text);
    layout->addSpacing(12);
    content = new QVBoxLayout;
    content->setSpacing(12);
    layout->addLayout(content, 1);
    return widget;
}

QFrame *card(QWidget *parent, QVBoxLayout *&content)
{
    QFrame *frame = M3::card(QStringLiteral("card"), parent);
    content = new QVBoxLayout(frame);
    content->setContentsMargins(18, 16, 18, 16);
    content->setSpacing(12);
    return frame;
}

QHBoxLayout *labeledRow(const QString &label, QWidget *parent)
{
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(label, parent));
    row->addStretch(1);
    return row;
}

} // namespace

FirstRunDialog::FirstRunDialog(const QString &detectedBackendName, QWidget *parent) : M3Dialog(parent)
{
    setWindowTitle(tr("Welcome to Athena"));
    setMinimumSize(720, 640);
    const bool arch = DistroSupport::detectDistroFamily() == DistroSupport::DistroFamily::Arch
        && ProcessRunner::executableExists(QStringLiteral("pacman"));
    const int totalSteps = 5;
    auto stepLabel = [totalSteps](int n) { return tr("STEP %1 OF %2").arg(n).arg(totalSteps); };

    m_pages = new QStackedWidget(this);

    // 1. Welcome.
    {
        auto *welcome = new QWidget;
        auto *layout = new QVBoxLayout(welcome);
        layout->setContentsMargins(0, 24, 0, 0);
        layout->setSpacing(14);
        auto *mark = new QLabel(welcome);
        auto paintMark = [mark]() { mark->setPixmap(appMark(96)); };
        paintMark();
        connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, mark, paintMark);
        layout->addWidget(mark, 0, Qt::AlignHCenter);
        auto *title = M3::label(tr("Welcome to Athena"), QStringLiteral("display"), welcome);
        title->setAlignment(Qt::AlignHCenter);
        layout->addWidget(title);
        auto *intro = M3::label(tr("One place for your system's packages, the AUR, Flatpak, and Snap — searched together, "
                                   "updated together. Let's set it up the way you like it; it takes a minute."),
                                QStringLiteral("muted"), welcome);
        intro->setAlignment(Qt::AlignHCenter);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto *detected = M3::label(!detectedBackendName.isEmpty()
                                       ? tr("Detected %1").arg(detectedBackendName)
                                       : tr("No supported system package manager (DNF, APT, or Pacman) was detected."),
                                   QStringLiteral("label"), welcome);
        detected->setAlignment(Qt::AlignHCenter);
        layout->addWidget(detected);
        layout->addStretch(1);
        m_pages->addWidget(welcome);
    }

    // 2. Look.
    {
        QVBoxLayout *content = nullptr;
        m_pages->addWidget(page(stepLabel(2), tr("Choose a look"),
                                tr("Material You colors everything from one accent you pick. Breeze matches KDE Plasma. "
                                   "Switch any time in Settings."),
                                content));
        auto *cards = new QHBoxLayout;
        cards->setSpacing(12);
        auto *material = new OptionCard(QStringLiteral("palette"), tr("Material You"),
                                        tr("Rounded, colorful, generated from your accent color."), this);
        auto *breeze = new OptionCard(QStringLiteral("desktop_windows"), tr("Breeze"),
                                      tr("Native KDE widgets and your desktop's colors."), this);
        auto *lookGroup = new QButtonGroup(this);
        lookGroup->addButton(material, 0);
        lookGroup->addButton(breeze, 1);
        (AppSettings::instance().lookStyle() == LookStyle::Material ? material : breeze)->setChecked(true);
        cards->addWidget(material);
        cards->addWidget(breeze);
        content->addLayout(cards);

        QVBoxLayout *options = nullptr;
        content->addWidget(card(this, options));
        options->addWidget(M3::label(tr("Accent color"), QStringLiteral("titleM"), this));
        QWidget *accent = M3::accentPicker(this);
        accent->setEnabled(AppSettings::instance().lookStyle() == LookStyle::Material);
        options->addWidget(accent);
        QHBoxLayout *themeRow = labeledRow(tr("Theme"), this);
        // Index order matches ThemeMode's declaration order (System, Light, Dark).
        QButtonGroup *theme = M3::segmented({{tr("System"), QStringLiteral("contrast")},
                                             {tr("Light"), QStringLiteral("light_mode")},
                                             {tr("Dark"), QStringLiteral("dark_mode")}},
                                            this, themeRow);
        theme->button(static_cast<int>(AppSettings::instance().themeMode()))->setChecked(true);
        options->addLayout(themeRow);
        content->addStretch(1);

        connect(lookGroup, &QButtonGroup::idClicked, this, [accent](int id) {
            AppSettings::instance().setLookStyle(id == 0 ? LookStyle::Material : LookStyle::Breeze);
            accent->setEnabled(id == 0);
        });
        connect(theme, &QButtonGroup::idClicked, this,
                [](int id) { AppSettings::instance().setThemeMode(static_cast<ThemeMode>(id)); });
    }

    // 3. Sources.
    {
        QVBoxLayout *content = nullptr;
        m_pages->addWidget(page(stepLabel(3), tr("Where your apps come from"),
                                tr("Athena searches and updates every source you have, side by side. Add Flatpak or Snap "
                                   "here if you'd like more apps to choose from."),
                                content));
        QVBoxLayout *formats = nullptr;
        content->addWidget(card(this, formats));
        formats->addWidget(M3::label(tr("Flatpak and Snap"), QStringLiteral("titleM"), this));
        formats->addWidget(new PackageFormatInstallWidget(this));
        if (arch) {
            QVBoxLayout *aur = nullptr;
            content->addWidget(card(this, aur));
            aur->addWidget(M3::label(tr("Arch User Repository"), QStringLiteral("titleM"), this));
            aur->addWidget(new AurHelperInstallWidget(this));
        }
        content->addStretch(1);
    }

    // 4. Preferences.
    {
        QVBoxLayout *content = nullptr;
        m_pages->addWidget(page(stepLabel(4), tr("A few preferences"), tr("Small things that make Athena feel like yours."), content));
        QVBoxLayout *prefs = nullptr;
        content->addWidget(card(this, prefs));
        QHBoxLayout *startRow = labeledRow(tr("Open Athena on"), this);
        auto *startupTabCombo = new QComboBox(this);
        // Index order matches StartupTab's declaration order.
        startupTabCombo->addItems({tr("Home"), tr("Discover"), tr("Installed"), tr("Updates"), tr("Sources"), tr("Tools")});
        startupTabCombo->setCurrentIndex(static_cast<int>(AppSettings::instance().startupTab()));
        startRow->addWidget(startupTabCombo);
        prefs->addLayout(startRow);

        auto addCheck = [this, prefs](const QString &text, bool checked, std::function<void(bool)> apply) {
            auto *check = new QCheckBox(text, this);
            check->setChecked(checked);
            connect(check, &QCheckBox::toggled, this, [apply](bool on) { apply(on); });
            prefs->addWidget(check);
        };
        addCheck(tr("Compact rows in package lists"), AppSettings::instance().compactPackageLists(),
                 [](bool on) { AppSettings::instance().setCompactPackageLists(on); });
        addCheck(tr("Animations"), AppSettings::instance().animationsEnabled(),
                 [](bool on) { AppSettings::instance().setAnimationsEnabled(on); });
        addCheck(tr("Use Athena's own title bar"), AppSettings::instance().useCustomTitleBar(),
                 [](bool on) { AppSettings::instance().setUseCustomTitleBar(on); });
        addCheck(tr("Check for Athena updates on startup"), AppSettings::instance().checkForAppUpdatesOnStartup(),
                 [](bool on) { AppSettings::instance().setCheckForAppUpdatesOnStartup(on); });
        content->addStretch(1);
        connect(startupTabCombo, &QComboBox::currentIndexChanged, this,
                [](int index) { AppSettings::instance().setStartupTab(static_cast<StartupTab>(index)); });
    }

    // 5. Done.
    {
        auto *done = new QWidget;
        auto *layout = new QVBoxLayout(done);
        layout->setContentsMargins(0, 40, 0, 0);
        layout->setSpacing(14);
        auto *icon = new QLabel(done);
        auto paintIcon = [icon]() {
            icon->setPixmap(MaterialTheme::icon(QStringLiteral("check_circle"), MaterialTheme::current().primary, 72, true).pixmap(72, 72));
        };
        paintIcon();
        connect(MaterialTheme::notifier(), &MaterialTheme::Notifier::changed, icon, paintIcon);
        layout->addWidget(icon, 0, Qt::AlignHCenter);
        auto *title = M3::label(tr("You're all set"), QStringLiteral("display"), done);
        title->setAlignment(Qt::AlignHCenter);
        layout->addWidget(title);
        m_doneSummary = M3::label(QString(), QStringLiteral("muted"), done);
        m_doneSummary->setAlignment(Qt::AlignHCenter);
        m_doneSummary->setWordWrap(true);
        layout->addWidget(m_doneSummary);
        auto *hint = M3::label(tr("Everything here lives in Settings, and this setup can be run again from there."),
                               QStringLiteral("label"), done);
        hint->setAlignment(Qt::AlignHCenter);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        layout->addStretch(1);
        m_pages->addWidget(done);
    }

    body()->addWidget(m_pages, 1);

    // Footer: skip on the left, dots in the middle, back/next on the right.
    m_skip = addAction(tr("Skip setup"), QStringLiteral("text"));
    actionRow()->insertWidget(0, m_skip);
    m_dots = new PageDots(m_pages->count(), this);
    actionRow()->insertWidget(1, m_dots, 0, Qt::AlignVCenter);
    actionRow()->insertStretch(1, 1);
    m_back = addAction(tr("Back"), QStringLiteral("text"), QStringLiteral("arrow_back"));
    m_next = addAction(tr("Get started"), QStringLiteral("filled"), QStringLiteral("chevron_right"));
    // A forward arrow belongs after the label.
    m_next->setLayoutDirection(Qt::RightToLeft);

    connect(m_skip, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_back, &QPushButton::clicked, this, [this]() { goTo(m_pages->currentIndex() - 1); });
    connect(m_next, &QPushButton::clicked, this, [this]() {
        if (m_pages->currentIndex() == m_pages->count() - 1)
            accept();
        else
            goTo(m_pages->currentIndex() + 1);
    });

    // However it closes — finishing, skipping, Escape — it should never be
    // shown again on its own (it can still be reopened from Settings).
    connect(this, &QDialog::finished, this, []() { AppSettings::instance().setHasCompletedFirstRun(true); });
    goTo(0);
}

void FirstRunDialog::goTo(int index)
{
    if (index < 0 || index >= m_pages->count())
        return;
    const bool last = index == m_pages->count() - 1;
    if (last) {
        const AppSettings &settings = AppSettings::instance();
        const QString look = settings.lookStyle() == LookStyle::Material ? tr("Material You") : tr("Breeze");
        const QString theme = settings.themeMode() == ThemeMode::System ? tr("following your system")
            : settings.themeMode() == ThemeMode::Light                  ? tr("light")
                                                                        : tr("dark");
        m_doneSummary->setText(tr("Athena will use %1, %2. Your packages from every source are a click away.").arg(look, theme));
    }
    Motion::switchStack(m_pages, index);
    m_dots->setCurrent(index);
    m_back->setVisible(index > 0);
    m_skip->setVisible(!last);
    m_next->setText(index == 0 ? tr("Get started") : last ? tr("Start using Athena") : tr("Next"));
    MaterialTheme::setButtonIcon(m_next, last ? QStringLiteral("check") : QStringLiteral("chevron_right"));
    m_next->setFocus();
}
