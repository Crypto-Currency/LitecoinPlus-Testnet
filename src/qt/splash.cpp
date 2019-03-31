#include "splash.h"
#include "ui_splash.h"
#include "clientmodel.h"

#include "version.h"

Splash::Splash(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Splash)
{
    ui->setupUi(this);
	// this->setStyleSheet("background-color: #ceffee;");
}

void Splash::setModel(ClientModel *model)
{
    if(model)
    {
        ui->label->setText(model->formatFullVersion());
    }
}

Splash::~Splash()
{
    delete ui;
}

//void AboutDialog::on_buttonBox_accepted()
//{
//    close();
//}
