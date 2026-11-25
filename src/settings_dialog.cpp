#include "settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace endpaper {

SettingsDialog::SettingsDialog(Config& config, AkonadiFetcher& fetcher, QWidget* parent)
    : QDialog(parent)
    , m_config(config)
    , m_fetcher(fetcher)
    , m_akonadiAvailable(fetcher.is_available())
{
    setWindowTitle("Settings");
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
    toggleTokenBtn->setCheckable(true);
    toggleTokenBtn->setToolTip("Show / Hide API token");
    connect(toggleTokenBtn, &QToolButton::toggled, this, [this, toggleTokenBtn](bool shown) {
        m_mfTokenEdit->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
        toggleTokenBtn->setText(shown ? "Hide" : "Show");
    });
    tokenRow->addWidget(m_mfTokenEdit);
    tokenRow->addWidget(toggleTokenBtn);
    mfForm->addRow("API Token:", tokenRow);

    rootLayout->addWidget(mfGroup);

    // ── Defaults ─────────────────────────────────────────────────────────
    auto* defaultsGroup = new QGroupBox("Defaults", this);
    auto* defaultsForm = new QFormLayout(defaultsGroup);

    m_authorEdit = new QLineEdit(QString::fromStdString(m_config.get("epub", "author", "Unknown")), this);
    defaultsForm->addRow("Default Author:", m_authorEdit);

    m_publisherEdit = new QLineEdit(QString::fromStdString(m_config.get("epub", "publisher", "Endpaper")), this);
    defaultsForm->addRow("Default Publisher:", m_publisherEdit);

    auto* folderRow = new QHBoxLayout();
    folderRow->setContentsMargins(0, 0, 0, 0);
    m_defaultFolderEdit = new QLineEdit(QString::fromStdString(m_config.get("general", "default_folder", "")), this);
    m_defaultFolderEdit->setPlaceholderText("Default (~/Documents)");
    auto* browseBtn = new QPushButton("Browse...", this);
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::browseDefaultFolder);
    folderRow->addWidget(m_defaultFolderEdit);
    folderRow->addWidget(browseBtn);
    defaultsForm->addRow("Output Folder:", folderRow);

    m_rssMaxSpin = new QSpinBox(this);
    m_rssMaxSpin->setRange(0, 10000);
    m_rssMaxSpin->setSpecialValueText("0 (unlimited)");
    m_rssMaxSpin->setValue(std::clamp(m_config.get_int("rss", "max_articles", 50), 0, 10000));
    defaultsForm->addRow("RSS Max Articles:", m_rssMaxSpin);

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

    // ── System ───────────────────────────────────────────────────────────
    auto* sysGroup = new QGroupBox("System", this);
    auto* sysForm = new QFormLayout(sysGroup);

    auto* akonadiRow = new QHBoxLayout();
    akonadiRow->setContentsMargins(0, 0, 0, 0);
    m_akonadiLabel = new QLabel(this);
    auto* refreshAkonadiBtn = new QPushButton("Refresh", this);
    refreshAkonadiBtn->setIcon(QIcon::fromTheme("view-refresh"));
    connect(refreshAkonadiBtn, &QPushButton::clicked, this, &SettingsDialog::refreshAkonadiStatus);
    akonadiRow->addWidget(m_akonadiLabel, 1);
    akonadiRow->addWidget(refreshAkonadiBtn);
    sysForm->addRow("Akonadi:", akonadiRow);

    auto* cfgLabel = new QLabel(QString::fromStdString(m_config.config_file().string()), this);
    cfgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sysForm->addRow("Config File:", cfgLabel);

    updateAkonadiLabel();
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

void SettingsDialog::updateAkonadiLabel() {
    m_akonadiLabel->setText(m_akonadiAvailable
        ? "Connected"
        : "Disconnected (start with: akonadictl start)");
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
