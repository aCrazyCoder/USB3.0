#include "USB30.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	USB30 w;
	w.show();
	return a.exec();
}
