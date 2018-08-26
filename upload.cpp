#include "upload.h"

uploadDialog::uploadDialog(QWidget *parent, QFile *package) : QDialog(parent)
{
	setupUi(this);

	layout()->setSizeConstraint(QLayout::SetFixedSize);
	setWindowFlags(windowFlags() | Qt::MSWindowsFixedSizeDialogHint);

	file = package;

	label_file->setText(QFileInfo(file->fileName()).fileName());
	label_bytes->setText(QString("0 / %1 Byte").arg(size));

	connect(&timer, SIGNAL(timeout()), this, SLOT(timer_refreshTime()));

	finished = false;
	canceled = false;

	time.start();
	timer.start(500);

	QTimer::singleShot(1, this, SLOT(startUploading()));
}

void uploadDialog::startUploading()
{
	pkg = file->readAll();
	file->close();

	size = pkg.size();

	server = new QTcpServer(this);
	socket = 0;

	connect(server, SIGNAL(newConnection()), this, SLOT(newConnection()));

	if(server->listen(QHostAddress(((MainWindow*)parent())->src_ip), 8080))
	{
		if(((MainWindow*)parent())->sendUDP(MIIO_GET_CURRENT_SOUND))
		{
			((MainWindow*)parent())->sendUDP(QString(MIIO_DNLD_INSTALL_SOUND).arg(QString("http://192.168.1.1:8080/%1").arg(QFileInfo(file->fileName()).fileName())).arg(QString(QCryptographicHash::hash(pkg, QCryptographicHash::Md5).toHex())).arg(((MainWindow*)parent())->robo.currentsound.sid_in_use).arg("%1"));
		}
	}
	else
	{
		timer.stop();

		canceled = true;

		QMessageBox::warning(this, APPNAME, tr("Could not start HTTP server!\n\n%1").arg(server->errorString()));

		close();
	}
}

void uploadDialog::timer_refreshTime()
{
	QTime elapsed = QTime::fromMSecsSinceStartOfDay(time.elapsed());

	label_time->setText(QString("%1h:%2m:%3s").arg(elapsed.hour(), 2, 10, QChar('0')).arg(elapsed.minute(), 2, 10, QChar('0')).arg(elapsed.second(), 2, 10, QChar('0')));
}

void uploadDialog::newConnection()
{
	socket = server->nextPendingConnection();

	connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
	connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
	connect(socket, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWritten(qint64)));
}

void uploadDialog::readyRead()
{
	socket->readAll();

	header = QString("HTTP/1.1 200 OK\r\nDate: %1 GMT\r\nContent-Type: application/octet-stream\r\nContent-Length: %2\r\nConnection: close\r\n\r\n").arg(QLocale("en_US").toString(QDateTime::currentDateTime().toUTC(), "ddd, dd MMM yyyy hh:mm:ss")).arg(size).toUtf8();

	total = header.size() * -1;

	socket->write(header);
	socket->write(pkg);
}

void uploadDialog::bytesWritten(qint64 byte)
{
	total += byte;

	progressBar_upload->setValue((100 * total) / size);
	label_bytes->setText(QString("%1 / %2 Byte").arg(total).arg(size));
}

void uploadDialog::disconnected()
{
	finished = true;

	socket->close();
	server->close();

	if(canceled)
	{
		timer.stop();

		QMessageBox::warning(this, APPNAME, tr("Upload aborted by user!"));

		close();

		return;
	}

	progressBar_install->setValue(50);

	while(((MainWindow*)parent())->robo.soundprogress.sid_in_progress)
	{
		((MainWindow*)parent())->sendUDP(MIIO_GET_SOUND_PROGRESS);

		QThread::msleep(1000);
		QCoreApplication::processEvents();
	}

	timer.stop();

	progressBar_install->setValue(100);

	((MainWindow*)parent())->robo.soundprogress.error ? QMessageBox::warning(this, APPNAME, tr("Voice upload failed!\n\nErrorcode: %1").arg(((MainWindow*)parent())->robo.soundprogress.error)) : QMessageBox::information(this, APPNAME, tr("Voice upload sucessfull."));

	close();
}

void uploadDialog::reject()
{
	if(!finished && !canceled)
	{
		if(QMessageBox::question(this, APPNAME, tr("Really abort upload?"), QMessageBox::Yes | QMessageBox::No,  QMessageBox::Yes) ==  QMessageBox::Yes)
		{
			canceled = true;

			if(socket)
			{
				socket->abort();

				reject();
			}
			else
			{
				server->close();

				accept();
			}
		}
	}
	else
	{
		accept();
	}
}

void uploadDialog::on_buttonBox_clicked(__attribute__((unused)) QAbstractButton *button)
{
	close();
}
