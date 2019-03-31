#ifndef SPLASH_H
#define SPLASH_H

#include <QDialog>

namespace Ui {
    class Splash;
}
class ClientModel;

/** "About" dialog box */
class Splash : public QDialog
{
    Q_OBJECT

public:
    explicit Splash(QWidget *parent = 0);
    ~Splash();

    void setModel(ClientModel *model);
private:
    Ui::Splash *ui;

private slots:
//    void on_buttonBox_accepted();
};

#endif // SPLASH_H
