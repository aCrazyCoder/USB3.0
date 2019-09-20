#pragma once

#include "windows.h"
#include "CyAPI.h"
#include <QtWidgets/QMainWindow>
#include <QTime>
#include <QStandardPaths>
#include <QSettings>
#include <QThread>
#include <QMessageBox>
#include <QTextBrowser>
#include <QLayout>
#include <QPushButton>
#include <QIcon>

class USB30 : public QMainWindow
{
	Q_OBJECT

public:
	USB30(QWidget *parent = Q_NULLPTR);

private:
	QWidget *centralWidget;
	QGridLayout *gridLayout;
	static QTextBrowser *textBrowser;
	static QPushButton *pushButton;
	QMenuBar *menuBar;

	QThread			*XferThread;
	CCyUSBDevice	*USBDevice;

	static const int		VENDOR_ID = 0x04B4;
	static const int		PRODUCT_ID = 0x00F1;
	static CCyUSBEndPoint		*EndPt;
	CCyUSBEndPoint* bulkInEndPt;
	CCyUSBEndPoint* bulkOutEndPt;

	static bool					bStreaming;
	static bool					bHighSpeedDevice;
	static bool					bSuperSpeedDevice;
	static bool					bAppQuiting;

	static HANDLE				m_hFile;
	static HANDLE				m_hMap;
	static PUCHAR				lpbMapAddress;

	void getComputerInfo();
	void getUSBDevice();
	static bool mmpInit();
	CCyUSBEndPoint* getEndPt();
	bool downloadConfigDataFile();
	static void XferLoop();
	static void AbortXferLoop(PUCHAR *buffers, PUCHAR *contexts, OVERLAPPED inOvLap);

protected:
	void closeEvent(QCloseEvent *event);

private slots:
	void StartBtn_Click();

};

