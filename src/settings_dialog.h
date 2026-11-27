#pragma once

#include "akonadi_fetcher.h"
#include "config.h"
#include "database.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

namespace endpaper {

/// Modal checklist dialog for toggling KMail account and folder visibility.
class FolderVisibilityDialog : public QDialog {
    Q_OBJECT

public:
    FolderVisibilityDialog(Database& db,
                           const std::vector<FolderInfo>& folders,
                           QWidget* parent = nullptr);

public slots:
    void showAll();
    void hideAll();
    void hideSystemFolders();
    void saveAndAccept();

private:
    void buildTree(const std::vector<FolderInfo>& folders, const FolderPrefs& prefs);
    void applyFilter(const QString& query);
    void collectHidden(QTreeWidgetItem* item, std::unordered_set<int64_t>& hiddenIds) const;

    Database& m_db;
    QLineEdit* m_filterEdit = nullptr;
    QTreeWidget* m_tree = nullptr;
};

/// Compact modal dialog for editing persisted Endpaper preferences.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(Config& config,
                   Database& db,
                   AkonadiFetcher& fetcher,
                   const std::vector<FolderInfo>& folders,
                   QWidget* parent = nullptr);

    [[nodiscard]] bool akonadiAvailable() const { return m_akonadiAvailable; }
    [[nodiscard]] bool foldersChanged() const { return m_foldersChanged; }

private slots:
    void browseDefaultFolder();
    void refreshAkonadiStatus();
    void manageKMailFolders();
    void saveAndAccept();

private:
    void updateAkonadiLabel();
    void updateFolderCountLabel();

    Config& m_config;
    Database& m_db;
    AkonadiFetcher& m_fetcher;
    const std::vector<FolderInfo>& m_folders;
    bool m_akonadiAvailable = false;
    bool m_foldersChanged = false;

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
    QLabel* m_folderCountLabel = nullptr;
};

} // namespace endpaper
