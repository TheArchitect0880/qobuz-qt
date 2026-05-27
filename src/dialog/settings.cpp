#include "settings.hpp"
#include "../util/settings.hpp"
#include "../scrobbler/lastfm.hpp"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QNetworkReply>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setMinimumWidth(420);

    m_nam = new QNetworkAccessManager(this);

    auto *layout = new QVBoxLayout(this);

    // --- Playback group ---
    auto *playGroup  = new QGroupBox(tr("Playback"), this);
    auto *playLayout = new QFormLayout(playGroup);

    m_formatBox = new QComboBox(playGroup);
    m_formatBox->addItem(tr("Hi-Res 24-bit/192kHz"), 27);
    m_formatBox->addItem(tr("Hi-Res 24-bit/96kHz"),  7);
    m_formatBox->addItem(tr("CD 16-bit"),             6);
    m_formatBox->addItem(tr("MP3 320 kbps"),          5);

    const int currentFormat = AppSettings::instance().preferredFormat();
    for (int i = 0; i < m_formatBox->count(); ++i) {
        if (m_formatBox->itemData(i).toInt() == currentFormat) {
            m_formatBox->setCurrentIndex(i);
            break;
        }
    }
    playLayout->addRow(tr("Preferred quality:"), m_formatBox);

    m_replayGain = new QCheckBox(tr("Enable ReplayGain (track gain normalisation)"), playGroup);
    m_replayGain->setChecked(AppSettings::instance().replayGainEnabled());
    playLayout->addRow(m_replayGain);

    m_gapless = new QCheckBox(tr("Gapless playback"), playGroup);
    m_gapless->setChecked(AppSettings::instance().gaplessEnabled());
    playLayout->addRow(m_gapless);

    layout->addWidget(playGroup);

    auto *downloadGroup = new QGroupBox(tr("Downloads"), this);
    auto *downloadLayout = new QFormLayout(downloadGroup);

    auto *folderRow = new QWidget(downloadGroup);
    auto *folderRowLayout = new QHBoxLayout(folderRow);
    folderRowLayout->setContentsMargins(0, 0, 0, 0);
    folderRowLayout->setSpacing(8);
    m_downloadFolder = new QLineEdit(AppSettings::instance().downloadFolder(), folderRow);
    auto *browseDownloadFolder = new QPushButton(tr("Browse…"), folderRow);
    folderRowLayout->addWidget(m_downloadFolder, 1);
    folderRowLayout->addWidget(browseDownloadFolder);
    downloadLayout->addRow(tr("Download folder:"), folderRow);

    m_downloadFormatBox = new QComboBox(downloadGroup);
    m_downloadFormatBox->addItem(tr("Hi-Res 24-bit/192kHz"), 27);
    m_downloadFormatBox->addItem(tr("Hi-Res 24-bit/96kHz"), 7);
    m_downloadFormatBox->addItem(tr("CD 16-bit"), 6);
    m_downloadFormatBox->addItem(tr("MP3 320 kbps"), 5);
    const int currentDownloadFormat = AppSettings::instance().downloadFormat();
    for (int i = 0; i < m_downloadFormatBox->count(); ++i) {
        if (m_downloadFormatBox->itemData(i).toInt() == currentDownloadFormat) {
            m_downloadFormatBox->setCurrentIndex(i);
            break;
        }
    }
    downloadLayout->addRow(tr("Download quality:"), m_downloadFormatBox);

    m_sourceSubdirs = new QCheckBox(tr("Create a Qobuz subfolder under the download folder"), downloadGroup);
    m_sourceSubdirs->setChecked(AppSettings::instance().downloadSourceSubdirectories());
    downloadLayout->addRow(m_sourceSubdirs);

    m_discSubdirs = new QCheckBox(tr("Create Disc N subfolders for multi-disc albums"), downloadGroup);
    m_discSubdirs->setChecked(AppSettings::instance().downloadDiscSubdirectories());
    downloadLayout->addRow(m_discSubdirs);

    m_singlesToFolder = new QCheckBox(tr("Put single tracks into album-style folders"), downloadGroup);
    m_singlesToFolder->setChecked(AppSettings::instance().downloadAddSinglesToFolder());
    downloadLayout->addRow(m_singlesToFolder);

    m_renumberPlaylistTracks = new QCheckBox(tr("Renumber playlist tracks from 1..N"), downloadGroup);
    m_renumberPlaylistTracks->setChecked(AppSettings::instance().downloadRenumberPlaylistTracks());
    downloadLayout->addRow(m_renumberPlaylistTracks);

    m_setPlaylistToAlbum = new QCheckBox(tr("Use playlist name as album metadata"), downloadGroup);
    m_setPlaylistToAlbum->setChecked(AppSettings::instance().downloadSetPlaylistToAlbum());
    downloadLayout->addRow(m_setPlaylistToAlbum);

    m_restrictCharacters = new QCheckBox(tr("Restrict filenames to printable ASCII"), downloadGroup);
    m_restrictCharacters->setChecked(AppSettings::instance().downloadRestrictCharacters());
    downloadLayout->addRow(m_restrictCharacters);

    m_saveArtwork = new QCheckBox(tr("Save cover.jpg in album folders"), downloadGroup);
    m_saveArtwork->setChecked(AppSettings::instance().downloadSaveArtwork());
    downloadLayout->addRow(m_saveArtwork);

    m_embedArtwork = new QCheckBox(tr("Embed artwork in downloaded audio files"), downloadGroup);
    m_embedArtwork->setChecked(AppSettings::instance().downloadEmbedArtwork());
    downloadLayout->addRow(m_embedArtwork);

    m_truncateTo = new QSpinBox(downloadGroup);
    m_truncateTo->setMinimum(0);
    m_truncateTo->setMaximum(255);
    m_truncateTo->setValue(AppSettings::instance().downloadTruncateTo());
    m_truncateTo->setSpecialValueText(tr("No limit"));
    downloadLayout->addRow(tr("Filename truncate length:"), m_truncateTo);

    layout->addWidget(downloadGroup);

    // --- Last.fm group ---
    auto *lfmGroup  = new QGroupBox(tr("Last.fm Scrobbling"), this);
    auto *lfmLayout = new QFormLayout(lfmGroup);

    m_lastFmEnabled = new QCheckBox(tr("Enable scrobbling"), lfmGroup);
    m_lastFmEnabled->setChecked(AppSettings::instance().lastFmEnabled());
    lfmLayout->addRow(m_lastFmEnabled);

    m_lastFmApiKey = new QLineEdit(AppSettings::instance().lastFmApiKey(), lfmGroup);
    m_lastFmApiKey->setPlaceholderText(tr("Get one at last.fm/api"));
    lfmLayout->addRow(tr("API Key:"), m_lastFmApiKey);

    m_lastFmApiSecret = new QLineEdit(AppSettings::instance().lastFmApiSecret(), lfmGroup);
    m_lastFmApiSecret->setEchoMode(QLineEdit::Password);
    lfmLayout->addRow(tr("API Secret:"), m_lastFmApiSecret);

    m_lastFmUsername = new QLineEdit(lfmGroup);
    m_lastFmUsername->setPlaceholderText(tr("Last.fm username"));
    lfmLayout->addRow(tr("Username:"), m_lastFmUsername);

    m_lastFmPassword = new QLineEdit(lfmGroup);
    m_lastFmPassword->setEchoMode(QLineEdit::Password);
    m_lastFmPassword->setPlaceholderText(tr("Last.fm password"));
    lfmLayout->addRow(tr("Password:"), m_lastFmPassword);

    auto *connectRow = new QHBoxLayout;
    m_lastFmConnect = new QPushButton(tr("Connect"), lfmGroup);
    m_lastFmStatus  = new QLabel(lfmGroup);

    const bool hasSession = !AppSettings::instance().lastFmSessionKey().isEmpty();
    m_lastFmStatus->setText(hasSession ? tr("Connected ✓") : tr("Not connected"));
    m_lastFmStatus->setStyleSheet(hasSession ? "color: green;" : "color: gray;");

    connectRow->addWidget(m_lastFmConnect);
    connectRow->addWidget(m_lastFmStatus, 1);
    lfmLayout->addRow(connectRow);

    layout->addWidget(lfmGroup);
    layout->addStretch();

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(m_lastFmConnect, &QPushButton::clicked, this, &SettingsDialog::onLastFmConnect);
    connect(browseDownloadFolder, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this,
            tr("Choose download folder"),
            m_downloadFolder->text().trimmed());
        if (!dir.isEmpty())
            m_downloadFolder->setText(dir);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { applyChanges(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::applyChanges()
{
    AppSettings::instance().setPreferredFormat(m_formatBox->currentData().toInt());
    AppSettings::instance().setReplayGainEnabled(m_replayGain->isChecked());
    AppSettings::instance().setGaplessEnabled(m_gapless->isChecked());
    AppSettings::instance().setDownloadFolder(m_downloadFolder->text().trimmed());
    AppSettings::instance().setDownloadFormat(m_downloadFormatBox->currentData().toInt());
    AppSettings::instance().setDownloadSourceSubdirectories(m_sourceSubdirs->isChecked());
    AppSettings::instance().setDownloadDiscSubdirectories(m_discSubdirs->isChecked());
    AppSettings::instance().setDownloadAddSinglesToFolder(m_singlesToFolder->isChecked());
    AppSettings::instance().setDownloadRenumberPlaylistTracks(m_renumberPlaylistTracks->isChecked());
    AppSettings::instance().setDownloadSetPlaylistToAlbum(m_setPlaylistToAlbum->isChecked());
    AppSettings::instance().setDownloadRestrictCharacters(m_restrictCharacters->isChecked());
    AppSettings::instance().setDownloadSaveArtwork(m_saveArtwork->isChecked());
    AppSettings::instance().setDownloadEmbedArtwork(m_embedArtwork->isChecked());
    AppSettings::instance().setDownloadTruncateTo(m_truncateTo->value());
    AppSettings::instance().setLastFmEnabled(m_lastFmEnabled->isChecked());
    AppSettings::instance().setLastFmApiKey(m_lastFmApiKey->text().trimmed());
    AppSettings::instance().setLastFmApiSecret(m_lastFmApiSecret->text().trimmed());
}

void SettingsDialog::onLastFmConnect()
{
    const QString apiKey    = m_lastFmApiKey->text().trimmed();
    const QString apiSecret = m_lastFmApiSecret->text().trimmed();
    const QString username  = m_lastFmUsername->text().trimmed();
    const QString password  = m_lastFmPassword->text();

    if (apiKey.isEmpty() || apiSecret.isEmpty() || username.isEmpty() || password.isEmpty()) {
        m_lastFmStatus->setText(tr("Fill in all fields first."));
        m_lastFmStatus->setStyleSheet("color: red;");
        return;
    }

    // Temporarily apply so the scrobbler util can use them for signing
    AppSettings::instance().setLastFmApiKey(apiKey);
    AppSettings::instance().setLastFmApiSecret(apiSecret);

    m_lastFmConnect->setEnabled(false);
    m_lastFmStatus->setText(tr("Connecting…"));
    m_lastFmStatus->setStyleSheet("color: gray;");

    // Reuse LastFmScrobbler::authenticate as a utility (temp instance)
    auto *tmp = new LastFmScrobbler(this);
    tmp->authenticate(username, password, [this, tmp](bool ok, const QString &err) {
        tmp->deleteLater();
        m_lastFmConnect->setEnabled(true);
        if (ok) {
            m_lastFmStatus->setText(tr("Connected ✓"));
            m_lastFmStatus->setStyleSheet("color: green;");
        } else {
            m_lastFmStatus->setText(tr("Error: %1").arg(err));
            m_lastFmStatus->setStyleSheet("color: red;");
        }
    });
}
