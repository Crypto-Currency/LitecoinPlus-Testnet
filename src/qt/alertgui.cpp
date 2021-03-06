#include "alertgui.h"
#include "ui_alertgui.h"
#include "util.h"

#include "init.h"
#include "alert.h"

#include <inttypes.h>

#include <QClipboard>
#include <QMessageBox>
#include <QMenu>
#include <QFileDialog>
#include <QProgressDialog>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QSettings>
#include <QSize>
#include <boost/filesystem.hpp>
#include <string>
#include <QMainWindow>
#include <QDateTime>
#include <QHeaderView>

#include <QDebug>
using namespace std;


//  ------------------------------------------------------------------------ //
// function:     AlertGui(QWidget *parent)
// what it does: class contructor
// access:       implicit
// return:       nothing
//  ------------------------------------------------------------------------ //
AlertGui::AlertGui(QWidget *parent) : QWidget(parent), ui(new Ui::AlertGui)
{
	// build the UI
	ui->setupUi(this);
	ui->mainLayout->setSizeConstraint(QLayout::SetNoConstraint);

	// create and add block list
	createAlertsList();
	ui->mainLayout->addWidget(alertsTable, 3, 0, 1, 5);


/*	ui->dustAddress->setReadOnly(true);


	// some general info label
	infoLabel = new QLabel();
	ui->mainLayout->addWidget(infoLabel, 4, 0, 1, 5);

	// create and add refresh button
	QPushButton *refreshButton = new QPushButton(tr("&Refresh"));
	connect(refreshButton, SIGNAL(clicked()), this, SLOT(updateBlockList()));
	ui->mainLayout->addWidget(refreshButton, 5, 0);

	// create and add dust button
	QPushButton *dustButton = new QPushButton(tr("&Dust now"));
	connect(dustButton, SIGNAL(clicked()), this, SLOT(compactBlocks()));
	ui->mainLayout->addWidget(dustButton, 5, 4);

	// load settings
	minimumBlockAmount = 16;
	blockDivisor = 50;



//	ui->mainLayout->addWidget(fileLabel, 0, 0);
//	ui->mainLayout->addWidget(fileComboBox, 0, 1, 1, 2);
//	ui->mainLayout->addWidget(textLabel, 1, 0);
//	ui->mainLayout->addWidget(textComboBox, 1, 1, 1, 2);
//	ui->mainLayout->addWidget(directoryLabel, 2, 0);
//	ui->mainLayout->addWidget(directoryComboBox, 2, 1);
//	ui->mainLayout->addWidget(browseButton, 2, 2);
//	ui->mainLayout->addWidget(filesFoundLabel, 4, 0, 1, 2);
//	ui->mainLayout->addWidget(findButton, 4, 2);
//	ui->mainLayout->addWidget(resetButton, 5, 2);

//	updateBlockList();*/
}

//  ------------------------------------------------------------------------ //
// function:     createAlertList()
// what it does: creates the table view for the blocks on screen
// access:       private
// return:       void
//  ------------------------------------------------------------------------ //
void AlertGui::createAlertsList()
{
	alertsTable = new QTableWidget(0, 5);
	alertsTable->setSelectionBehavior(QAbstractItemView::SelectRows);


	QTableWidgetItem *id = new QTableWidgetItem();
	id->setTextAlignment(Qt::AlignLeft);
	id->setText("#");
	QTableWidgetItem *date = new QTableWidgetItem();
	date->setTextAlignment(Qt::AlignLeft);
	date->setText("Date");
	QTableWidgetItem *type = new QTableWidgetItem();
	type->setTextAlignment(Qt::AlignLeft);
	type->setText("Type");
	QTableWidgetItem *priority = new QTableWidgetItem();
	priority->setTextAlignment(Qt::AlignLeft);
	priority->setText("Priority");
	QTableWidgetItem *msg = new QTableWidgetItem();
	msg->setTextAlignment(Qt::AlignLeft);
	msg->setText("Message");

	alertsTable->setHorizontalHeaderItem(COLUMN_ID, id);
	alertsTable->setHorizontalHeaderItem(COLUMN_DATE, date);
	alertsTable->setHorizontalHeaderItem(COLUMN_TYPE, type);
	alertsTable->setHorizontalHeaderItem(COLUMN_PRIORITY, priority);
	alertsTable->setHorizontalHeaderItem(COLUMN_MESSAGE, msg);


/*	QStringList labels;
	labels << tr("#") << tr("Date") << tr("Type") << tr("Priority") << tr("Message");
	alertsTable->setHorizontalHeaderLabels(labels);*/

	alertsTable->setColumnWidth(COLUMN_ID, 40);
	alertsTable->setColumnWidth(COLUMN_DATE, 190);
	alertsTable->setColumnWidth(COLUMN_TYPE, 130);
	alertsTable->setColumnWidth(COLUMN_PRIORITY, 60);
	alertsTable->setColumnWidth(COLUMN_MESSAGE, 410);

	alertsTable->verticalHeader()->hide();
	alertsTable->setShowGrid(true);
	//alertsTable->setSelectionMode(QAbstractItemView::MultiSelection);

//	connect(filesTable, SIGNAL(cellActivated(int,int)),
//		this, SLOT(openFileOfItem(int,int)));
}

//  ------------------------------------------------------------------------ //
// function:     updateAlertsList()
// what it does: creates the table view for the blocks on screen
// access:       private
// return:       void
//  ------------------------------------------------------------------------ //
void AlertGui::updateAlertsList()
{
// prepare to refresh
    alertsTable->setRowCount(0);
    alertsTable->setEnabled(false);
    alertsTable->setAlternatingRowColors(true);

// prepare some basic data
	QIcon *icInfo = new QIcon(":/icons/alert_info");
	QIcon *icWarning = new QIcon(":/icons/alert_warning");
	QIcon *icCritical = new QIcon(":/icons/alert_critical");
	QIcon *icSuperCritical = new QIcon(":/icons/alert_supercritical");

// loop alerts ordered by ID, with semaphore
	{
        LOCK(cs_mapAlerts);
        for (map<int, CAlert>::iterator mi = mapAlertsById.begin(); mi != mapAlertsById.end(); mi++)
        {
			const CAlert& alert = (*mi).second;

		// alerts with priority = 999 are special, they are rules set, we skip those
			if (CAlert::isRule(alert.nPriority))
			{
				continue;
			}

		// create new items
			QTableWidgetItem *idItem = new QTableWidgetItem();
			idItem->setFlags(idItem->flags() ^ Qt::ItemIsEditable);
			QTableWidgetItem *dateItem = new QTableWidgetItem();
			dateItem->setFlags(dateItem->flags() ^ Qt::ItemIsEditable);
			QTableWidgetItem *typeItem = new QTableWidgetItem();
			typeItem->setFlags(typeItem->flags() ^ Qt::ItemIsEditable);
			QTableWidgetItem *priorityItem = new QTableWidgetItem();
			priorityItem->setFlags(priorityItem->flags() ^ Qt::ItemIsEditable);
			QTableWidgetItem *msgItem = new QTableWidgetItem();
			msgItem->setFlags(msgItem->flags() ^ Qt::ItemIsEditable);


		// fill the items
			idItem->setText(QString::number(alert.nID));
			dateItem->setText((QDateTime::fromTime_t(alert.nReceivedOn)).toString()); //QString::fromStdString(DateTimeStrFormat("%x %H:%M:%S", GetTime())));
			if (CAlert::isInfo(alert.nPriority))
			{
				typeItem->setIcon(*icInfo);
				typeItem->setText(tr("Info"));
				msgItem->setText(QString::fromStdString(alert.strComment));
			}
			else if (CAlert::isWarning(alert.nPriority))
			{
				typeItem->setIcon(*icWarning);
				typeItem->setText(tr("Warning"));
				msgItem->setText(QString::fromStdString(alert.strComment));
			}
			else if (CAlert::isCritical(alert.nPriority))
			{
				typeItem->setIcon(*icCritical);
				typeItem->setText(tr("Critical"));
				msgItem->setText(QString::fromStdString(alert.strComment));
			}
			else if (CAlert::isSuperCritical(alert.nPriority))
			{
				typeItem->setIcon(*icSuperCritical);
				typeItem->setText(tr("URGENT!"));
				msgItem->setText(QString::fromStdString(alert.strStatusBar));
			}
			typeItem->setTextAlignment(Qt::AlignVCenter);
			priorityItem->setText(QString::number(alert.nPriority));

		// link the items to a new row
			int row = alertsTable->rowCount();
			alertsTable->insertRow(row);
			alertsTable->setItem(row, COLUMN_ID, idItem);
			alertsTable->setItem(row, COLUMN_DATE, dateItem);
			alertsTable->setItem(row, COLUMN_TYPE, typeItem);
			alertsTable->setItem(row, COLUMN_PRIORITY, priorityItem);
			alertsTable->setItem(row, COLUMN_MESSAGE, msgItem);
		}
	}

// sort view to default
    alertsTable->setEnabled(true);
}

//  ------------------------------------------------------------------------ //
// function:     sortView(int column, Qt::SortOrder order)
// what it does: sort view as desired
// access:       private
// return:       void
//  ------------------------------------------------------------------------ //
void AlertGui::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    alertsTable->sortByColumn(column, order);
    alertsTable->horizontalHeader()->setSortIndicator(sortColumn, sortOrder);
}

//  ------------------------------------------------------------------------ //
// function:     resizeEvent(QResizeEvent* event)
// what it does: run on resize
// access:       protected
// return:       void
//  ------------------------------------------------------------------------ //
void AlertGui::resizeEvent(QResizeEvent* event)
{
// resize the inner table when necessary
//	int ww = width();
//	filesTable->setColumnWidth(1, ww - 278);
}

//  ------------------------------------------------------------------------ //
// function:     showEvent(QShowEvent* event)
// what it does: run on show
// access:       protected
// return:       void
//  ------------------------------------------------------------------------ //
void AlertGui::showEvent(QShowEvent* event)
{
// update once manually, and subscribe to core signals
	updateAlertsList();
	subscribeToCoreSignals();
}

//  ------------------------------------------------------------------------ //
// function:     hideEvent(QHideEvent* event)
// what it does: run on hide
// access:       protected
// return:       void
//  ------------------------------------------------------------------------ //
void AlertGui::hideEvent(QHideEvent* event)
{
// disconnect core signals
	unsubscribeFromCoreSignals();
}

//  ------------------------------------------------------------------------ //
// function:     alertMapUpdated()
// what it does: this is an event, is executed when the map alert is modified
// access:       slot
// return:       void
//  ------------------------------------------------------------------------ //
void AlertGui::alertMapUpdated()
{
	updateAlertsList();
}

// following method to handle, connect and disconnect events

static void NotifyAlertMapUpdated(AlertGui *alertGui, const uint256 &hash, ChangeType status)
{
    QMetaObject::invokeMethod(alertGui, "alertMapUpdated", Qt::QueuedConnection);
}

void AlertGui::subscribeToCoreSignals()
{
    // Connect signals to client
#if BOOST_VERSION >= 107000
	uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertMapUpdated, this, boost::placeholders::_1, boost::placeholders::_2));
#else
	uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertMapUpdated, this, _1, _2));
#endif
}

void AlertGui::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
#if BOOST_VERSION >= 107000
	uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertMapUpdated, this, boost::placeholders::_1, boost::placeholders::_2));
#else
	uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertMapUpdated, this, _1, _2));
#endif
}

