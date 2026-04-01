#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QNetworkAccessManager>

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
