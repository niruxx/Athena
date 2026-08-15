cmake_minimum_required(VERSION 3.16)
project(distore-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

qt_standard_project_setup()

add_executable(distore-qt
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp

    src/core/PackageInfo.h
    src/core/PackageGroupInfo.h
    src/core/RepositoryInfo.h
    src/core/PackageBackend.h
    src/core/BackendFactory.h
    src/core/BackendFactory.cpp
    src/core/ProcessRunner.h
    src/core/ProcessRunner.cpp
    src/core/AppSettings.h
    src/core/AppSettings.cpp
    src/core/Version.h
    src/core/GitHubReleaseChecker.h
    src/core/GitHubReleaseChecker.cpp

    src/core/backends/DnfBackend.h
    src/core/backends/DnfBackend.cpp
    src/core/backends/AptBackend.h
    src/core/backends/AptBackend.cpp
    src/core/backends/PacmanBackend.h
    src/core/backends/PacmanBackend.cpp
    src/core/backends/FlatpakBackend.h
    src/core/backends/FlatpakBackend.cpp

    src/models/PackageTableModel.h
    src/models/PackageTableModel.cpp

    src/ui/AddRepositoryDialog.h
    src/ui/AddRepositoryDialog.cpp
    src/ui/AppIcons.h
    src/ui/HistoryPage.h
    src/ui/HistoryPage.cpp
    src/ui/InstalledPage.h
    src/ui/InstalledPage.cpp
    src/ui/SearchPage.h
    src/ui/SearchPage.cpp
    src/ui/GroupsPage.h
    src/ui/GroupsPage.cpp
    src/ui/PackageActions.h
    src/ui/PackageActions.cpp
    src/ui/PackageBrowser.h
    src/ui/PackageBrowser.cpp
    src/ui/RepositoriesPage.h
    src/ui/RepositoriesPage.cpp
    src/ui/SettingsPage.h
    src/ui/SettingsPage.cpp
    src/ui/TerminalOutputDialog.h
    src/ui/TerminalOutputDialog.cpp
    src/ui/TransactionConfirmDialog.h
    src/ui/TransactionConfirmDialog.cpp
    src/ui/UpdateBannerWidget.h
    src/ui/UpdateBannerWidget.cpp
    src/ui/UpdatesPage.h
    src/ui/UpdatesPage.cpp
)

target_link_libraries(distore-qt PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network)

install(TARGETS distore-qt RUNTIME DESTINATION bin)
install(FILES dist/distore-qt.desktop DESTINATION share/applications)
install(FILES assets/icon.svg DESTINATION share/icons/hicolor/scalable/apps RENAME distore-qt.svg)
install(FILES assets/icon-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME distore-qt.png)
install(FILES assets/icon-128.png DESTINATION share/icons/hicolor/128x128/apps RENAME distore-qt.png)
install(FILES assets/icon-64.png DESTINATION share/icons/hicolor/64x64/apps RENAME distore-qt.png)

# --- Packaging (CPack: RPM for Fedora, DEB for Debian) ---

file(STRINGS "${CMAKE_SOURCE_DIR}/src/core/Version.h" DISTORE_QT_VERSION_LINE REGEX "DISTORE_QT_VERSION")
string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1" DISTORE_QT_VERSION "${DISTORE_QT_VERSION_LINE}")

set(CPACK_PACKAGE_NAME "distore-qt")
set(CPACK_PACKAGE_VERSION "${DISTORE_QT_VERSION}")
set(CPACK_PACKAGE_VENDOR "niruxxdaboi")
set(CPACK_PACKAGE_CONTACT "niruxxdaboi <perezkyle98@protonmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-distro GUI package manager for DNF, APT, Pacman, and Flatpak")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/niruxx/distore-qt")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_PACKAGE_CHECKSUM SHA256)

# RPM (Fedora / RHEL)
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3+")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_PACKAGE_AUTOREQPROV YES)
set(CPACK_RPM_PACKAGE_REQUIRES "polkit")
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")

# DEB (Debian / Ubuntu) — built here on Fedora, so dpkg-shlibdeps has no
# Debian shlibs database to query; dependencies are listed explicitly
# instead of relying on auto-detection.
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "niruxxdaboi <perezkyle98@protonmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "admin")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6, libqt6gui6, libqt6widgets6, libqt6network6, libqt6concurrent6, libc6, policykit-1 | polkitd")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

include(CPack)
