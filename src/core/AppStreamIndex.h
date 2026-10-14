#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

// A searchable index of every desktop application the system's software
// sources say they offer — installed or not — built from the AppStream
// catalogs distros and Flatpak remotes already ship:
//   - distro catalogs: /usr/share/swcatalog/xml (Arch's archlinux-appstream-data,
//     Fedora), the older /usr/share/app-info/xmls, and Debian/Ubuntu's DEP-11
//     YAML under /usr/share/swcatalog/yaml and /var/lib/app-info/yaml;
//   - Flatpak: <installation>/appstream/<remote>/<arch>/active/appstream.xml
//     for the system and user installations.
// Nothing is downloaded: catalogs refresh with the distro package that
// ships them (archlinux-appstream-data, appstream-data...) and with
// `flatpak update --appstream`.
struct AppStreamApp {
    enum class Kind { Native, Flatpak };
    Kind kind = Kind::Native;
    QString id;          // component ID
    QString package;     // native package name, or Flatpak app ID
    QString name;
    QString summary;
    QString description; // plain text, paragraphs separated by blank lines
    QString developer;
    QString license;
    QString homepage;
    QString version;     // newest release listed, if any
    QString origin;      // catalog origin, or Flatpak remote name
    QString iconPath;    // cached icon file, if the catalog has one
    QString stockIcon;   // icon theme name, if the catalog names one
    QStringList categories;
    QStringList keywords;
    QStringList screenshots; // image URLs (remote), up to four
};

class AppStreamIndex : public QObject {
    Q_OBJECT

public:
    explicit AppStreamIndex(QObject *parent = nullptr);

    // Parses every catalog in the background; loaded() fires when done
    // (also on a reload). Safe to call again to pick up refreshed catalogs.
    void load();
    bool isLoaded() const { return m_loaded; }
    int size() const { return m_apps.size(); }

    // Best matches first: name, then package/ID, keywords, and summary.
    QVector<const AppStreamApp *> search(const QString &query, int limit = 300) const;
    // Apps in a freedesktop main category ("AudioVideo", "Development", ...).
    QVector<const AppStreamApp *> inCategory(const QString &category) const;
    const AppStreamApp *findNative(const QString &package) const;
    const AppStreamApp *findFlatpak(const QString &appId) const;

signals:
    void loaded();

private:
    QVector<AppStreamApp> m_apps;
    QHash<QString, int> m_nativeByPackage;
    QHash<QString, int> m_flatpakById;
    bool m_loaded = false;
    int m_generation = 0;
};
