#include "settings.hpp"
#include "../util/settings.hpp"
#include "../scrobbler/lastfm.hpp"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
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
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { applyChanges(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::applyChanges()
{
    AppSettings::instance().setPreferredFormat(m_formatBox->currentData().toInt());
    AppSettings::instance().setReplayGainEnabled(m_replayGain->isChecked());
    AppSettings::instance().setGaplessEnabled(m_gapless->isChecked());
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
