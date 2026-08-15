#pragma once

#include <memory>

#include <QMainWindow>

#include "core/PackageBackend.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    std::unique_ptr<PackageBackend> m_backend;
    std::unique_ptr<PackageBackend> m_flatpakBackend;
    std::unique_ptr<PackageBackend> m_snapBackend;
    class UpdateBannerWidget *m_updateBanner = nullptr;
};
