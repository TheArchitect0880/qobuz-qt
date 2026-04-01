#include "login.hpp"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Sign in to Qobuz"));
    setMinimumWidth(360);
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // Logo / title
    auto *title = new QLabel(QStringLiteral("<h2>Qobuz</h2>"), this);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    // Form
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);

    m_email    = new QLineEdit(this);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);

    form->addRow(tr("E-mail:"),   m_email);
    form->addRow(tr("Password:"), m_password);
    layout->addLayout(form);

    // Remember checkbox
    m_remember = new QCheckBox(tr("Remember me"), this);
    m_remember->setChecked(true);
    layout->addWidget(m_remember);

    // Error label (hidden until needed)
    m_errorLbl = new QLabel(this);
    m_errorLbl->setStyleSheet("color: red;");
    m_errorLbl->setWordWrap(true);
    m_errorLbl->hide();
    layout->addWidget(m_errorLbl);

    // Buttons
    m_loginBtn = new QPushButton(tr("Sign in"), this);
    m_loginBtn->setDefault(true);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(m_loginBtn);
    layout->addLayout(btnLayout);

    connect(m_loginBtn, &QPushButton::clicked, this, [this] {
        m_errorLbl->hide();
        emit loginRequested(m_email->text().trimmed(), m_password->text());
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Allow pressing Enter in the password field
    connect(m_password, &QLineEdit::returnPressed, m_loginBtn, &QPushButton::click);
}

void LoginDialog::setError(const QString &msg)
{
    m_errorLbl->setText(msg);
    m_errorLbl->show();
    setBusy(false);
}

void LoginDialog::setBusy(bool busy)
{
    m_loginBtn->setEnabled(!busy);
    m_loginBtn->setText(busy ? tr("Signing in…") : tr("Sign in"));
    m_email->setEnabled(!busy);
    m_password->setEnabled(!busy);
}
