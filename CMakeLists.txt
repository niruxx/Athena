cmake_minimum_required(VERSION 3.16)
project(Athena LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

qt_standard_project_setup()

add_executable(athena
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
    src/core/DistroSupport.h
    src/core/DistroSupport.cpp
    src/core/Version.h
    src/core/GitHubReleaseChecker.h
    src/core/GitHubReleaseChecker.cpp
    src/core/Logger.h
    src/core/Logger.cpp
    src/core/DirSizeScanner.h
    src/core/DirSizeScanner.cpp
    src/core/UserCleanupUtils.h
    src/core/UserCleanupUtils.cpp

    src/core/backends/DnfBackend.h
    src/core/backends/DnfBackend.cpp
    src/core/backends/AptBackend.h
    src/core/backends/AptBackend.cpp
    src/core/backends/PacmanBackend.h
    src/core/backends/PacmanBackend.cpp
    src/core/backends/FlatpakBackend.h
    src/core/backends/FlatpakBackend.cpp
    src/core/backends/SnapBackend.h
    src/core/backends/SnapBackend.cpp

    src/models/PackageTableModel.h
    src/models/PackageTableModel.cpp

    src/ui/AddRepositoryDialog.h
    src/ui/AddRepositoryDialog.cpp
    src/ui/AppIcons.h
    src/ui/BackupRestorePage.h
    src/ui/BackupRestorePage.cpp
    src/ui/FirstRunDialog.h
    src/ui/FirstRunDialog.cpp
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
    src/ui/PackageFormatInstallWidget.h
    src/ui/PackageFormatInstallWidget.cpp
    src/ui/PermissionsPage.h
    src/ui/PermissionsPage.cpp
    src/ui/UserDataPage.h
    src/ui/UserDataPage.cpp
    src/ui/LeftoverDataPage.h
    src/ui/LeftoverDataPage.cpp
    src/ui/PreferencesDialog.h
    src/ui/PreferencesDialog.cpp
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
    src/ui/UserCleanupPage.h
    src/ui/UserCleanupPage.cpp
    src/ui/UserBackupRestorePage.h
    src/ui/UserBackupRestorePage.cpp
)

target_link_libraries(athena PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network)

install(TARGETS athena RUNTIME DESTINATION bin)
install(FILES dist/athena.desktop DESTINATION share/applications)
install(FILES assets/icon.svg DESTINATION share/icons/hicolor/scalable/apps RENAME athena.svg)
install(FILES assets/icon-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME athena.png)
install(FILES assets/icon-128.png DESTINATION share/icons/hicolor/128x128/apps RENAME athena.png)
install(FILES assets/icon-64.png DESTINATION share/icons/hicolor/64x64/apps RENAME athena.png)

# --- Packaging (CPack: RPM for Fedora, DEB for Debian) ---

file(STRINGS "${CMAKE_SOURCE_DIR}/src/core/Version.h" ATHENA_VERSION_LINE REGEX "ATHENA_VERSION")
string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1" ATHENA_VERSION "${ATHENA_VERSION_LINE}")

set(CPACK_PACKAGE_NAME "athena")
set(CPACK_PACKAGE_VERSION "${ATHENA_VERSION}")
set(CPACK_PACKAGE_VENDOR "niruxxdaboi")
set(CPACK_PACKAGE_CONTACT "niruxxdaboi <perezkyle98@protonmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-distro GUI package manager for DNF, APT, Pacman, and Flatpak")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/niruxx/Athena")
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
