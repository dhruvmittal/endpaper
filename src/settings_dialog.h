#pragma once

#include "akonadi_fetcher.h"
#include "config.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace endpaper {

/// Compact modal dialog for editing persisted Endpaper preferences.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(Config& config, AkonadiFetcher& fetcher, QWidget* parent = nullptr);

    [[nodiscard]] bool akonadiAvailable() const { return m_akonadiAvailable; }

private slots:
    void browseDefaultFolder();
    void refreshAkonadiStatus();
    void saveAndAccept();

private:
    void updateAkonadiLabel();

    Config& m_config;
    AkonadiFetcher& m_fetcher;
    bool m_akonadiAvailable = false;

    QLineEdit* m_mfUrlEdit = nullptr;
    QLineEdit* m_mfTokenEdit = nullptr;

    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_publisherEdit = nullptr;
    QLineEdit* m_defaultFolderEdit = nullptr;
    QSpinBox* m_rssMaxSpin = nullptr;

    QCheckBox* m_fuzzyCheck = nullptr;
    QCheckBox* m_autoKMailCheck = nullptr;
    QCheckBox* m_autoMinifluxCheck = nullptr;
    QCheckBox* m_cacheMetadataCheck = nullptr;
    QCheckBox* m_rememberStateCheck = nullptr;
    QCheckBox* m_resolveStubsCheck = nullptr;
    QSpinBox* m_stubThresholdSpin = nullptr;
    QSpinBox* m_requestDelaySpin = nullptr;

    QLabel* m_akonadiLabel = nullptr;
};

} // namespace endpaper
