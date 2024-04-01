#ifndef SIGNVERIFYMESSAGEDIALOG_H
#define SIGNVERIFYMESSAGEDIALOG_H

#include <QWidget>

namespace Ui {
    class SignVerifyMessageDialog;
}
class WalletModel;

QT_BEGIN_NAMESPACE
QT_END_NAMESPACE

class SignVerifyMessageDialog : public QWidget
{
    Q_OBJECT
private:
    SignVerifyMessageDialog(const SignVerifyMessageDialog &); // {}
    SignVerifyMessageDialog &operator=(const SignVerifyMessageDialog &); // {}
public:
    explicit SignVerifyMessageDialog(QWidget *parent = 0);
    ~SignVerifyMessageDialog();

    void setModel(WalletModel *model);
    void setAddress_SM(QString address);
    void setAddress_VM(QString address);

    void showTab_SM(bool fShow);
    void showTab_VM(bool fShow);

protected:
    bool eventFilter(QObject *object, QEvent *event);
    void keyPressEvent(QKeyEvent *);

private:
    Ui::SignVerifyMessageDialog *ui;
    WalletModel *model;

private slots:
    /* sign message */
    void on_addressBookButton_SM_clicked();
    void on_pasteButton_SM_clicked();
    void on_signMessageButton_SM_clicked();
    void on_copySignatureButton_SM_clicked();
    void on_clearButton_SM_clicked();
    /* verify message */
    void on_addressBookButton_VM_clicked();
    void on_verifyMessageButton_VM_clicked();
    void on_clearButton_VM_clicked();
};

#endif // SIGNVERIFYMESSAGEDIALOG_H
//@
