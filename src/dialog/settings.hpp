#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QSpinBox>

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private:
    // Playback
    QComboBox *m_formatBox      = nullptr;
    QCheckBox *m_replayGain     = nullptr;
    QCheckBox *m_gapless        = nullptr;

    // Downloads
    QLineEdit *m_downloadFolder = nullptr;
    QComboBox *m_downloadFormatBox = nullptr;
    QCheckBox *m_sourceSubdirs = nullptr;
    QCheckBox *m_discSubdirs = nullptr;
    QCheckBox *m_singlesToFolder = nullptr;
    QCheckBox *m_renumberPlaylistTracks = nullptr;
    QCheckBox *m_setPlaylistToAlbum = nullptr;
    QCheckBox *m_restrictCharacters = nullptr;
    QCheckBox *m_saveArtwork = nullptr;
    QCheckBox *m_embedArtwork = nullptr;
    QSpinBox  *m_truncateTo = nullptr;

    // Last.fm
    QCheckBox  *m_lastFmEnabled    = nullptr;
    QLineEdit  *m_lastFmApiKey     = nullptr;
    QLineEdit  *m_lastFmApiSecret  = nullptr;
    QLineEdit  *m_lastFmUsername   = nullptr;
    QLineEdit  *m_lastFmPassword   = nullptr;
    QPushButton *m_lastFmConnect   = nullptr;
    QLabel      *m_lastFmStatus    = nullptr;
    QNetworkAccessManager *m_nam   = nullptr;

    void applyChanges();
    void onLastFmConnect();
};
