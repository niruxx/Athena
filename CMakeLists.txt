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
