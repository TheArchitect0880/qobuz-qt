#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

    QString email()    const { return m_email->text(); }
    QString password() const { return m_password->text(); }
    bool    remember() const { return m_remember->isChecked(); }

    void setError(const QString &msg);
    void setBusy(bool busy);

signals:
    void loginRequested(const QString &email, const QString &password);

private:
    QLineEdit   *m_email    = nullptr;
    QLineEdit   *m_password = nullptr;
    QCheckBox   *m_remember = nullptr;
    QPushButton *m_loginBtn = nullptr;
    QLabel      *m_errorLbl = nullptr;
};
