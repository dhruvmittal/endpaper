#include "settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_map>

namespace endpaper {

namespace {

bool apply_visibility_filter(QTreeWidgetItem* item, const QString& query) {
    bool anyChildVisible = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (apply_visibility_filter(item->child(i), query)) {
            anyChildVisible = true;
        }
    }
    QString fullPath = item->data(0, Qt::UserRole + 2).toString();
    bool matchesSelf = query.isEmpty() || fullPath.contains(query, Qt::CaseInsensitive);
    bool visible = matchesSelf || anyChildVisible;
    item->setHidden(!visible);
    if (!query.isEmpty() && anyChildVisible) {
        item->setExpanded(true);
    }
    return visible;
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// FolderVisibilityDialog
// ═════════════════════════════════════════════════════════════════════════════

FolderVisibilityDialog::FolderVisibilityDialog(Database& db,
                                               const std::vector<FolderInfo>& folders,
                                               QWidget* parent)
    : QDialog(parent)
    , m_db(db)
{
    setWindowTitle("Manage Visible KMail Folders");
    resize(440, 520);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter folders");
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &FolderVisibilityDialog::applyFilter);
    layout->addWidget(m_filterEdit);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(16);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);

    auto* quickRow = new QHBoxLayout();
    auto* showAllBtn = new QPushButton("Show &All", this);
    auto* hideAllBtn = new QPushButton("Hide &All", this);
    auto* hideSystemBtn = new QPushButton("Hide &System Folders", this);
    hideSystemBtn->setToolTip("Uncheck Trash, Sent, Drafts, Outbox, Templates, and Junk folders");
    connect(showAllBtn, &QPushButton::clicked, this, &FolderVisibilityDialog::showAll);
    connect(hideAllBtn, &QPushButton::clicked, this, &FolderVisibilityDialog::hideAll);
    connect(hideSystemBtn, &QPushButton::clicked, this, &FolderVisibilityDialog::hideSystemFolders);
    quickRow->addWidget(showAllBtn);
    quickRow->addWidget(hideAllBtn);
    quickRow->addWidget(hideSystemBtn);
    quickRow->addStretch();
    layout->addLayout(quickRow);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &FolderVisibilityDialog::saveAndAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    buildTree(folders, m_db.load_folder_prefs());
    m_filterEdit->setFocus();
}

void FolderVisibilityDialog::buildTree(const std::vector<FolderInfo>& folders, const FolderPrefs& prefs) {
    m_tree->clear();

    std::unordered_map<int64_t, const FolderInfo*> infoById;
    infoById.reserve(folders.size());
    for (const auto& f : folders) {
        infoById.emplace(f.collection_id, &f);
    }

    auto isEffectivelyHidden = [&](int64_t id) {
        int64_t cur = id;
        for (int depth = 0; depth < 32 && cur != -1; ++depth) {
            if (prefs.hidden.count(cur)) return true;
            auto it = infoById.find(cur);
            if (it == infoById.end()) break;
            cur = it->second->parent_id;
        }
        return false;
    };

    std::unordered_map<int64_t, QTreeWidgetItem*> itemMap;
    itemMap.reserve(folders.size());

    for (const auto& f : folders) {
        auto* item = new QTreeWidgetItem();
        QString label = f.name.empty() ? QString::fromStdString(f.full_path) : QString::fromStdString(f.name);
        item->setText(0, label);
        item->setToolTip(0, QString::fromStdString(f.full_path));
        item->setData(0, Qt::UserRole, static_cast<qint64>(f.collection_id));
        item->setData(0, Qt::UserRole + 1, QString::fromStdString(f.name));
        item->setData(0, Qt::UserRole + 2, QString::fromStdString(f.full_path));
        itemMap.emplace(f.collection_id, item);
    }

    for (const auto& f : folders) {
        QTreeWidgetItem* item = itemMap[f.collection_id];
        auto parentIt = (f.parent_id != -1 && f.parent_id != f.collection_id)
            ? itemMap.find(f.parent_id)
            : itemMap.end();
        if (parentIt != itemMap.end()) {
            parentIt->second->addChild(item);
        } else {
            m_tree->addTopLevelItem(item);
        }
    }

    // Set tri-state flags once children exist, then set leaf check states bottom-up
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        Qt::ItemFlags f = Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (item->childCount() > 0) {
            f |= Qt::ItemIsAutoTristate;
        }
        item->setFlags(f);
    }

    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        if (item->childCount() == 0) {
            int64_t cid = item->data(0, Qt::UserRole).toLongLong();
            item->setCheckState(0, isEffectivelyHidden(cid) ? Qt::Unchecked : Qt::Checked);
        }
    }

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        m_tree->topLevelItem(i)->setExpanded(true);
    }
}

void FolderVisibilityDialog::applyFilter(const QString& query) {
    QString clean = query.trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        apply_visibility_filter(m_tree->topLevelItem(i), clean);
    }
}

void FolderVisibilityDialog::showAll() {
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->childCount() == 0) {
            (*it)->setCheckState(0, Qt::Checked);
        }
    }
}

void FolderVisibilityDialog::hideAll() {
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->childCount() == 0) {
            (*it)->setCheckState(0, Qt::Unchecked);
        }
    }
}

void FolderVisibilityDialog::hideSystemFolders() {
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        std::string name = item->data(0, Qt::UserRole + 1).toString().toStdString();
        if (is_system_mail_folder(name)) {
            item->setCheckState(0, Qt::Unchecked);
        }
    }
}

void FolderVisibilityDialog::collectHidden(QTreeWidgetItem* item,
                                           std::unordered_set<int64_t>& hiddenIds) const {
    if (item->checkState(0) == Qt::Unchecked) {
        hiddenIds.insert(item->data(0, Qt::UserRole).toLongLong());
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectHidden(item->child(i), hiddenIds);
    }
}

void FolderVisibilityDialog::saveAndAccept() {
    std::unordered_set<int64_t> hiddenIds;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        collectHidden(m_tree->topLevelItem(i), hiddenIds);
    }
    m_db.replace_hidden_folders(hiddenIds);
    accept();
}

// ═════════════════════════════════════════════════════════════════════════════
// SettingsDialog
// ═════════════════════════════════════════════════════════════════════════════

SettingsDialog::SettingsDialog(Config& config,
                               Database& db,
                               AkonadiFetcher& fetcher,
                               const std::vector<FolderInfo>& folders,
                               QWidget* parent)
    : QDialog(parent)
    , m_config(config)
    , m_db(db)
    , m_fetcher(fetcher)
    , m_folders(folders)
    , m_akonadiAvailable(fetcher.is_available())
{
    setWindowTitle("Configure — Endpaper");
    setMinimumWidth(560);

    auto* rootLayout = new QVBoxLayout(this);

    // ── Miniflux API ─────────────────────────────────────────────────────
    auto* mfGroup = new QGroupBox("Miniflux API", this);
    auto* mfForm = new QFormLayout(mfGroup);

    m_mfUrlEdit = new QLineEdit(QString::fromStdString(m_config.get("miniflux", "url", "")), this);
    m_mfUrlEdit->setPlaceholderText("https://miniflux.example.com");
    mfForm->addRow("Server URL:", m_mfUrlEdit);

    auto* tokenRow = new QHBoxLayout();
    tokenRow->setContentsMargins(0, 0, 0, 0);
    m_mfTokenEdit = new QLineEdit(QString::fromStdString(m_config.get("miniflux", "token", "")), this);
    m_mfTokenEdit->setPlaceholderText("API token");
    m_mfTokenEdit->setEchoMode(QLineEdit::Password);
    auto* toggleTokenBtn = new QToolButton(this);
    toggleTokenBtn->setText("Show");
    toggleTokenBtn->setIcon(QIcon::fromTheme("password-show-on"));
    toggleTokenBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggleTokenBtn->setAccessibleName("Show API token");
    toggleTokenBtn->setCheckable(true);
    toggleTokenBtn->setToolTip("Show or hide API token");
    connect(toggleTokenBtn, &QToolButton::toggled, this, [this, toggleTokenBtn](bool shown) {
        m_mfTokenEdit->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        toggleTokenBtn->setText(shown ? "Hide" : "Show");
        toggleTokenBtn->setIcon(QIcon::fromTheme(shown ? "password-show-off" : "password-show-on"));
        toggleTokenBtn->setAccessibleName(shown ? "Hide API token" : "Show API token");
    });
    tokenRow->addWidget(m_mfTokenEdit);
    tokenRow->addWidget(toggleTokenBtn);
    mfForm->addRow("API token:", tokenRow);

    rootLayout->addWidget(mfGroup);

    // ── Defaults ─────────────────────────────────────────────────────────
    auto* defaultsGroup = new QGroupBox("Defaults", this);
    auto* defaultsForm = new QFormLayout(defaultsGroup);

    m_authorEdit = new QLineEdit(QString::fromStdString(m_config.get("epub", "author", "Unknown")), this);
    defaultsForm->addRow("Default author:", m_authorEdit);

    m_publisherEdit = new QLineEdit(QString::fromStdString(m_config.get("epub", "publisher", "Endpaper")), this);
    defaultsForm->addRow("Default publisher:", m_publisherEdit);

    auto* folderRow = new QHBoxLayout();
    folderRow->setContentsMargins(0, 0, 0, 0);
    m_defaultFolderEdit = new QLineEdit(QString::fromStdString(m_config.get("general", "default_folder", "")), this);
    m_defaultFolderEdit->setPlaceholderText("Default (~/Documents)");
    auto* browseBtn = new QPushButton("&Browse…", this);
    browseBtn->setIcon(QIcon::fromTheme("folder-open"));
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::browseDefaultFolder);
    folderRow->addWidget(m_defaultFolderEdit);
    folderRow->addWidget(browseBtn);
    defaultsForm->addRow("Output folder:", folderRow);

    m_rssMaxSpin = new QSpinBox(this);
    m_rssMaxSpin->setRange(0, 10000);
    m_rssMaxSpin->setSpecialValueText("0 (unlimited)");
    m_rssMaxSpin->setValue(std::clamp(m_config.get_int("rss", "max_articles", 50), 0, 10000));
    defaultsForm->addRow("RSS max articles:", m_rssMaxSpin);

    rootLayout->addWidget(defaultsGroup);

    // ── Behavior & Fetching ──────────────────────────────────────────────
    auto* behaviorGroup = new QGroupBox("Behavior & Fetching", this);
    auto* behaviorLayout = new QVBoxLayout(behaviorGroup);

    auto* checksGrid = new QGridLayout();
    m_fuzzyCheck = new QCheckBox("Fuzzy article search (off = regex)", this);
    m_fuzzyCheck->setChecked(m_config.get_bool("ui", "fuzzy_search", true));

    m_autoKMailCheck = new QCheckBox("Auto-fetch KMail folders", this);
    m_autoKMailCheck->setChecked(m_config.get_bool("general", "auto_fetch_kmail", false));

    m_autoMinifluxCheck = new QCheckBox("Auto-fetch Miniflux categories", this);
    m_autoMinifluxCheck->setChecked(m_config.get_bool("general", "auto_fetch_miniflux", false));

    m_cacheMetadataCheck = new QCheckBox("Cache folders & categories locally", this);
    m_cacheMetadataCheck->setChecked(m_config.get_bool("general", "cache_metadata", true));

    m_rememberStateCheck = new QCheckBox("Remember active source, book & folder state", this);
    m_rememberStateCheck->setChecked(m_config.get_bool("general", "remember_state", true));

    m_resolveStubsCheck = new QCheckBox("Resolve short stub articles from URL", this);
    m_resolveStubsCheck->setChecked(m_config.get_bool("fetching", "enable_stub_resolving", true));

    checksGrid->addWidget(m_fuzzyCheck, 0, 0);
    checksGrid->addWidget(m_autoKMailCheck, 0, 1);
    checksGrid->addWidget(m_autoMinifluxCheck, 1, 0);
    checksGrid->addWidget(m_cacheMetadataCheck, 1, 1);
    checksGrid->addWidget(m_rememberStateCheck, 2, 0);
    checksGrid->addWidget(m_resolveStubsCheck, 2, 1);
    behaviorLayout->addLayout(checksGrid);

    auto* fetchRow = new QHBoxLayout();
    m_stubThresholdSpin = new QSpinBox(this);
    m_stubThresholdSpin->setRange(0, 1000000);
    m_stubThresholdSpin->setSingleStep(100);
    m_stubThresholdSpin->setValue(std::clamp(m_config.get_int("fetching", "stub_char_threshold", 1000), 0, 1000000));

    m_requestDelaySpin = new QSpinBox(this);
    m_requestDelaySpin->setRange(0, 60000);
    m_requestDelaySpin->setSingleStep(100);
    m_requestDelaySpin->setValue(std::clamp(m_config.get_int("fetching", "request_delay_ms", 1000), 0, 60000));

    fetchRow->addWidget(new QLabel("Stub min chars:", this));
    fetchRow->addWidget(m_stubThresholdSpin);
    fetchRow->addSpacing(12);
    fetchRow->addWidget(new QLabel("Request delay (ms):", this));
    fetchRow->addWidget(m_requestDelaySpin);
    fetchRow->addStretch();
    behaviorLayout->addLayout(fetchRow);

    rootLayout->addWidget(behaviorGroup);

    // ── System & KMail ───────────────────────────────────────────────────
    auto* sysGroup = new QGroupBox("System", this);
    auto* sysForm = new QFormLayout(sysGroup);

    auto* akonadiRow = new QHBoxLayout();
    akonadiRow->setContentsMargins(0, 0, 0, 0);
    m_akonadiLabel = new QLabel(this);
    auto* refreshAkonadiBtn = new QPushButton("&Refresh", this);
    refreshAkonadiBtn->setIcon(QIcon::fromTheme("view-refresh"));
    connect(refreshAkonadiBtn, &QPushButton::clicked, this, &SettingsDialog::refreshAkonadiStatus);
    akonadiRow->addWidget(m_akonadiLabel, 1);
    akonadiRow->addWidget(refreshAkonadiBtn);
    sysForm->addRow("Akonadi:", akonadiRow);

    auto* kmailFoldersRow = new QHBoxLayout();
    kmailFoldersRow->setContentsMargins(0, 0, 0, 0);
    m_folderCountLabel = new QLabel(this);
    auto* manageFoldersBtn = new QPushButton("&Manage Folders…", this);
    manageFoldersBtn->setIcon(QIcon::fromTheme("view-filter"));
    manageFoldersBtn->setEnabled(!m_folders.empty());
    if (m_folders.empty()) {
        manageFoldersBtn->setToolTip("Fetch KMail folders first to configure visibility");
    }
    connect(manageFoldersBtn, &QPushButton::clicked, this, &SettingsDialog::manageKMailFolders);
    kmailFoldersRow->addWidget(m_folderCountLabel, 1);
    kmailFoldersRow->addWidget(manageFoldersBtn);
    sysForm->addRow("KMail folders:", kmailFoldersRow);

    auto* cfgLabel = new QLabel(QString::fromStdString(m_config.config_file().string()), this);
    cfgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sysForm->addRow("Configuration file:", cfgLabel);

    updateAkonadiLabel();
    updateFolderCountLabel();
    rootLayout->addWidget(sysGroup);

    // ── Dialog Buttons ───────────────────────────────────────────────────
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);
}

void SettingsDialog::browseDefaultFolder() {
    QString initial = m_defaultFolderEdit->text().trimmed();
    QString chosen = QFileDialog::getExistingDirectory(this, "Select Default Output Folder", initial);
    if (!chosen.isEmpty()) {
        m_defaultFolderEdit->setText(chosen);
    }
}

void SettingsDialog::refreshAkonadiStatus() {
    m_akonadiAvailable = m_fetcher.is_available();
    updateAkonadiLabel();
}

void SettingsDialog::manageKMailFolders() {
    FolderVisibilityDialog dlg(m_db, m_folders, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_foldersChanged = true;
        updateFolderCountLabel();
    }
}

void SettingsDialog::updateAkonadiLabel() {
    m_akonadiLabel->setText(m_akonadiAvailable
        ? "Connected"
        : "Disconnected (start with: akonadictl start)");
}

void SettingsDialog::updateFolderCountLabel() {
    if (m_folders.empty()) {
        m_folderCountLabel->setText("No folders cached");
        return;
    }
    FolderPrefs prefs = m_db.load_folder_prefs();
    std::unordered_map<int64_t, int64_t> parentMap;
    parentMap.reserve(m_folders.size());
    for (const auto& f : m_folders) {
        parentMap.emplace(f.collection_id, f.parent_id);
    }
    int visible = 0;
    for (const auto& f : m_folders) {
        bool hidden = false;
        int64_t cur = f.collection_id;
        for (int depth = 0; depth < 32 && cur != -1; ++depth) {
            if (prefs.hidden.count(cur)) {
                hidden = true;
                break;
            }
            auto it = parentMap.find(cur);
            if (it == parentMap.end()) break;
            cur = it->second;
        }
        if (!hidden) ++visible;
    }
    m_folderCountLabel->setText(QString("%1 of %2 visible").arg(visible).arg(m_folders.size()));
}

void SettingsDialog::saveAndAccept() {
    QString author = m_authorEdit->text().trimmed();
    QString publisher = m_publisherEdit->text().trimmed();

    m_config.set("miniflux", "url", m_mfUrlEdit->text().trimmed().toStdString());
    m_config.set("miniflux", "token", m_mfTokenEdit->text().trimmed().toStdString());
    m_config.set("epub", "author", author.isEmpty() ? "Unknown" : author.toStdString());
    m_config.set("epub", "publisher", publisher.isEmpty() ? "Endpaper" : publisher.toStdString());
    m_config.set("general", "default_folder", m_defaultFolderEdit->text().trimmed().toStdString());
    m_config.set("rss", "max_articles", std::to_string(m_rssMaxSpin->value()));

    m_config.set("ui", "fuzzy_search", m_fuzzyCheck->isChecked() ? "true" : "false");
    m_config.set("general", "auto_fetch_kmail", m_autoKMailCheck->isChecked() ? "true" : "false");
    m_config.set("general", "auto_fetch_miniflux", m_autoMinifluxCheck->isChecked() ? "true" : "false");
    m_config.set("general", "cache_metadata", m_cacheMetadataCheck->isChecked() ? "true" : "false");
    m_config.set("general", "remember_state", m_rememberStateCheck->isChecked() ? "true" : "false");
    m_config.set("fetching", "enable_stub_resolving", m_resolveStubsCheck->isChecked() ? "true" : "false");
    m_config.set("fetching", "stub_char_threshold", std::to_string(m_stubThresholdSpin->value()));
    m_config.set("fetching", "request_delay_ms", std::to_string(m_requestDelaySpin->value()));

    m_config.save();
    accept();
}

} // namespace endpaper
