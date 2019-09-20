#include "USB30.h"
#define PPX			8
#define TimeOut		1500
#define CONFIGFILESIZE 16384

USB30::USB30(QWidget *parent)
	: QMainWindow(parent)
{
	if (this->objectName().isEmpty())
		this->setObjectName(QString::fromUtf8("this"));
	this->resize(549, 286);
	centralWidget = new QWidget(this);
	centralWidget->setObjectName(QString::fromUtf8("centralWidget"));
	gridLayout = new QGridLayout(centralWidget);
	gridLayout->setSpacing(6);
	gridLayout->setContentsMargins(11, 11, 11, 11);
	gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
	textBrowser = new QTextBrowser(centralWidget);
	textBrowser->setObjectName(QString::fromUtf8("textBrowser"));
	gridLayout->addWidget(textBrowser, 0, 0, 1, 1);
	pushButton = new QPushButton(centralWidget);
	pushButton->setObjectName(QString::fromUtf8("pushButton"));
	pushButton->setText(QString::fromLocal8Bit("开始"));
	gridLayout->addWidget(pushButton, 0, 1, 1, 1);
	this->setCentralWidget(centralWidget);
	this->setWindowTitle(QString::fromLocal8Bit("USB3.0测试"));
	this->setWindowIcon(QIcon(":/USB30/Resources/usb.ico"));
	QObject::connect(pushButton, SIGNAL(clicked()), this, SLOT(StartBtn_Click()));
	QMetaObject::connectSlotsByName(this);

	bulkInEndPt = NULL;
	bulkOutEndPt = NULL;

	getComputerInfo();
	getUSBDevice();
	
	XferThread = QThread::create(XferLoop);
	XferThread->setParent(this);
	//connect(XferThread, &QThread::finished, XferThread, &QObject::deleteLater);
}

QTextBrowser* USB30::textBrowser = NULL;
QPushButton* USB30::pushButton = NULL;
CCyUSBEndPoint* USB30::EndPt = NULL;
bool USB30::bStreaming = false;
bool USB30::bHighSpeedDevice = false;
bool USB30::bSuperSpeedDevice = false;
bool USB30::bAppQuiting = false;
HANDLE USB30::m_hFile = 0;
HANDLE USB30::m_hMap = 0;
PUCHAR USB30::lpbMapAddress = 0;

void USB30::getComputerInfo()
{
	DWORD size = 0;
	GetComputerName(NULL, &size); 
	wchar_t *name = new wchar_t[size];
	if (GetComputerName(name, &size))
	{
		textBrowser->append(QString::fromWCharArray(name));
	}
	else
	{
		textBrowser->append("Unknown");
	}
	delete[] name;
	QSettings *CPU = new QSettings("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", QSettings::NativeFormat);
	textBrowser->append(CPU->value("ProcessorNameString").toString());
	delete CPU;
	typedef BOOL(WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
	LPFN_ISWOW64PROCESS fnIsWow64Process;
	BOOL bIsWow64 = FALSE;
	fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process");
	if (NULL != fnIsWow64Process)
	{
		fnIsWow64Process(GetCurrentProcess(), &bIsWow64);
	}
	QString sysBit = "unknown";
	if (bIsWow64)
		sysBit = "64bit";
	else
		sysBit = "32bit";
	textBrowser->append(QSysInfo::prettyProductName() + " " + sysBit);
}

void USB30::getUSBDevice()
{
	pushButton->setEnabled(false);

	/*if (USBDevice)
	{
	USBDevice->Close();
	delete USBDevice;
	USBDevice = NULL;
	}*/

	USBDevice = new CCyUSBDevice((HANDLE)this->winId(), CYUSBDRV_GUID, true);
	if (USBDevice == NULL)
	{
		QMessageBox::warning(this, "USB Exception", QString::fromLocal8Bit("USB设备实例化失败"));
		return;
	}
	int n = USBDevice->DeviceCount();
	if (n == 0)
	{
		QMessageBox::warning(this, "USB Exception", QString::fromLocal8Bit("未发现USB设备"));
		pushButton->setEnabled(false);
		return;
	}
	for (int i = 0; i<n; i++)
	{
		USBDevice->Open(i);
		if ((USBDevice->VendorID == VENDOR_ID) && (USBDevice->ProductID == PRODUCT_ID))
		{
			pushButton->setEnabled(true);
			int interfaces = USBDevice->AltIntfcCount() + 1;
			bHighSpeedDevice = USBDevice->bHighSpeed;
			bSuperSpeedDevice = USBDevice->bSuperSpeed;
			for (int i = 0; i< interfaces; i++)
			{
				if (USBDevice->SetAltIntfc(i) == true)
				{
					int eptCnt = USBDevice->EndPointCount();
					textBrowser->append(QString::fromLocal8Bit("USB端点列表:"));

					for (int e = 1; e<eptCnt; e++)
					{
						CCyUSBEndPoint *ept = USBDevice->EndPoints[e];
						if ((ept->Attributes >= 1) && (ept->Attributes <= 3))
						{
							QString s;
							s.append((ept->Attributes == 1) ? "ISOC " :
								((ept->Attributes == 2) ? "BULK " : "INTR "));
							s.append(ept->bIn ? "IN, " : "OUT, ");
							s.append(QString::number(ept->MaxPktSize) + " Bytes, ");

							if (USBDevice->BcdUSB == USB30MAJORVER)
								s.append(QString::number(ept->ssmaxburst) + " MaxBurst, ");

							s.append("   (" + QString::number(i) + " - ");
							s.append("0x" + QString::number(ept->Address, 16) + ")");
							textBrowser->append(s);
						}
						if (ept->bIn && (ept->Attributes == 2))
						{
							bulkInEndPt = ept;
						}
						else
						{
							bulkOutEndPt = ept;
						}
					}
				}
			}
			if (bulkInEndPt == NULL || bulkOutEndPt == NULL)
			{
				textBrowser->append(QString::fromLocal8Bit("USB端点初始化失败"));
				pushButton->setEnabled(false);
			}
		}
	}
}

bool USB30::mmpInit()
{
	QTime current_time = QTime::currentTime();
	QString m_filePath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)+
						 QString("/dataAcq_")+current_time.toString("hh_mm_ss_zzz")+QString(".dat");
	LPCWSTR m_FileName = reinterpret_cast<const wchar_t *>(m_filePath.utf16());
	m_hFile = CreateFile(m_FileName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (m_hFile == INVALID_HANDLE_VALUE)
	{
		textBrowser->append(QString::fromLocal8Bit("创建内存映射文件失败"));
		return false;
	}
	m_hMap = CreateFileMapping(m_hFile, NULL, PAGE_READWRITE, 0, 512*1024*1024, NULL);
	if (m_hMap == NULL)
	{
		textBrowser->append(QString::fromLocal8Bit("创建文件映射对象失败"));
		CloseHandle(m_hFile);
		return false;
	}
	lpbMapAddress = (PUCHAR)MapViewOfFile(m_hMap, FILE_MAP_WRITE, 0, 0, 0);
	if (lpbMapAddress == NULL)
	{
		textBrowser->append(QString::fromLocal8Bit("映射文件失败"));
		CloseHandle(m_hMap);
		CloseHandle(m_hFile);
		return false;
	}
	textBrowser->append(QString::fromLocal8Bit("内存映射成功：")+QString::number(GetFileSize(m_hFile, NULL)/1024/1024)+"MB");
	return true;
}

/*
 *	下载开始文件不需要读取数据。
 *  下载停止文件需要下载完后，读取一个MaxPktSize大小数据包，以触发FPGA读取PC下传
 *  的配置数据包，这是由硬件决定的，以免在下传数据时，破坏上传数据；然后再读取一个
 *  MaxPktSize*PPX大小数据包，此数据包是配置未更新时最后一个数据包，下一个数据包才是
 *  配置更新后的第一个数据包。
 *  程序思路：如果下传数据线程没有在running，则表示要下传开始文件给下位机
 */
bool USB30::downloadConfigDataFile()
{
	if (XferThread)
	{
		if (XferThread->isRunning())
		{
			EndPt = bulkInEndPt;
			bStreaming = false;
			XferThread->wait(10);

			return true;
		}
		else
		{
			EndPt = bulkOutEndPt;
			QByteArray path = QString(QDir::currentPath() + "/Resources/16KB_start.txt").toLatin1();
			long configDataFileLength;
			ifstream configDataFile;
			configDataFile.open(path.data(), ios::binary);
			configDataFile.seekg(0, ios::end);
			configDataFileLength = configDataFile.tellg();
			configDataFile.seekg(0, ios::beg);
			if (configDataFileLength != CONFIGFILESIZE)
			{
				textBrowser->append(QString::fromLocal8Bit("配置数据文件有误"));
				pushButton->setEnabled(false);
				return false;
			}
			PCHAR buffer = new CHAR[CONFIGFILESIZE];
			configDataFile.read(buffer, CONFIGFILESIZE);
			configDataFile.close();
			EndPt->SetXferSize(CONFIGFILESIZE);
			if (EndPt->XferData((PUCHAR)buffer, configDataFileLength))
			{
				return true;
			}
			else
			{
				textBrowser->append(QString::fromLocal8Bit("下载配置数据失败"));
				return false;
			}
		}
	}
	else
	{
		QMessageBox::warning(this, "USB Exception", QString::fromLocal8Bit("数据采集线程创建失败"));
		pushButton->setEnabled(false);
		return false;
	}
}

void USB30::StartBtn_Click()
{
	if (downloadConfigDataFile() == false)
	{
		QMessageBox::warning(this, "USB Exception", QString::fromLocal8Bit("下载配置数据失败，请重试"));
		return;
	}
	if (XferThread->isRunning())
	{
		pushButton->setText(QString::fromLocal8Bit("开始"));
		bStreaming = false;
		XferThread->wait(10);
	}
	else
	{
		EndPt = bulkInEndPt;
		pushButton->setText(QString::fromLocal8Bit("停止"));
		bStreaming = true;
		if (XferThread->isFinished()) // Start-over
		{
			XferThread = QThread::create(&XferLoop);
			XferThread->setParent(this);
		}
		XferThread->start();
	} 
}

void USB30::XferLoop()
{
	//Allocated memory
	if (mmpInit() == false)
	{
		pushButton->setText(QString::fromLocal8Bit("开始"));
		return;
	}

	long len = EndPt->MaxPktSize * PPX;
	EndPt->SetXferSize(len);

	OVERLAPPED			inOvLap;
	PUCHAR				context;
	inOvLap.hEvent = CreateEvent(NULL, false, false, NULL);
	context = EndPt->BeginDataXfer(lpbMapAddress, len, &inOvLap);
	if (EndPt->NtStatus || EndPt->UsbdStatus)
	{
		textBrowser->append("Xfer request rejected. NTSTATUS = " + QString::number(EndPt->NtStatus, 16));
		AbortXferLoop(&lpbMapAddress, &context, inOvLap);
		return;
	}
	long BytesXferred = 0;
	for (;bStreaming;)
	{
		long rLen = len;	

		if (!EndPt->WaitForXfer(&inOvLap, TimeOut))
		{
			EndPt->Abort();
			if (EndPt->LastError == ERROR_IO_PENDING)
				WaitForSingleObject(inOvLap.hEvent, 2000);
		}
		
		 // BULK Endpoint		
		if (EndPt->FinishDataXfer(lpbMapAddress, rLen, &inOvLap, context))
		{
			BytesXferred += rLen;
			lpbMapAddress += rLen;
		}
		else
			textBrowser->append("Data xfer failed.");

		context = EndPt->BeginDataXfer(lpbMapAddress, len, &inOvLap);
		if (EndPt->NtStatus || EndPt->UsbdStatus)
		{
			textBrowser->append("Xfer request rejected. NTSTATUS = " + QString::number(EndPt->NtStatus, 16));
			AbortXferLoop(&lpbMapAddress, &context, inOvLap);
			return;
		}
		if (BytesXferred < 0) // Rollover - reset counters
		{
			BytesXferred = 0;
		}
		if (BytesXferred > 511*1024*1024) // Stop - FOR TEST 511MB < 512MB
		{
			pushButton->setText(QString::fromLocal8Bit("开始"));
			textBrowser->append(QString::fromLocal8Bit("当前数据采集完成"));
			break;
		}
	}  // End of the infinite loop
	AbortXferLoop(&lpbMapAddress, &context, inOvLap);
}

void USB30::AbortXferLoop(PUCHAR* buffers, PUCHAR* contexts, OVERLAPPED inOvLap)
{
	long len = EndPt->MaxPktSize * PPX;
	EndPt->Abort();
	EndPt->WaitForXfer(&inOvLap, TimeOut);
	EndPt->FinishDataXfer(*buffers, len, &inOvLap, *contexts);
	CloseHandle(inOvLap.hEvent);
	UnmapViewOfFile(lpbMapAddress);
	CloseHandle(m_hMap);
	CloseHandle(m_hFile);

	bStreaming = false;
	if (bAppQuiting == false)
	{
		pushButton->setText(QString::fromLocal8Bit("开始"));
		pushButton->update();
	}
}

void USB30::closeEvent(QCloseEvent *event)
{
	bAppQuiting = true;
	bStreaming = false;

	if (XferThread->isRunning())
	{
		XferThread->wait(10);
	}
}
