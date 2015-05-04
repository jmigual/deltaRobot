/// @file mainwindow.cpp Contains the MainWindow class implementation
#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    _axis(XJoystick::AxisCount),
    _axisV(XJoystick::AxisCount),
    _buts(XJoystick::ButtonCount),
    _butsV(XJoystick::ButtonCount),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    
    connect(&_joy, SIGNAL(changed()), this, SLOT(joyChanged()));
    connect(&_timer, SIGNAL(timeout()), this, SLOT(update()));
    connect(&_sT, SIGNAL(statusBar(QString)), 
            ui->statusbar, SLOT(showMessage(QString)));
    
    
    _timer.setInterval(10);
    _timer.start();
    
    // JOYSTICK
    QVector< QString > V(_joy.getAllAxis());
    // Adding axis
    QGridLayout *wL = new QGridLayout;
    for (int i = 0; i < XJoystick::AxisCount; ++i) {
        QHBoxLayout *L = new QHBoxLayout;
        L->addWidget(new QLabel(V[i].append(":"), this));
        _axis[i] = new QLabel("#");
        L->addWidget(_axis[i]);
        L->addStretch();
        wL->addLayout(L, i%3, i/3); 
    }
    ui->joyAxis->setLayout(wL);
    
    // Adding buttons
    wL = new QGridLayout;
    for (int i = 0; i < XJoystick::ButtonCount; ++i) {
        _buts[i] = new QLabel(QString::number(i + 1));
        wL->addWidget(_buts[i], i/8, i%8);
        _buts[i]->setEnabled(false);
        _buts[i]->hide();
    }
    ui->joyButs->setLayout(wL);
    ui->joyAxis->hide();
    ui->joyButs->hide();
    ui->line->hide();
    
    // Creating data Path    
    _dataP = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(_dataP);
    if (!dir.exists()) dir.mkpath(_dataP);
    
    read();
    _sT.start();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;
    if (event->key() == Qt::Key_A) _joy.axisPress(0, -100);
    else if (event->key() == Qt::Key_D) _joy.axisPress(0, 100);
    else if (event->key() == Qt::Key_W) _joy.axisPress(1, 100);
    else if (event->key() == Qt::Key_S) _joy.axisPress(1, -100);
    
    this->update();
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;
    if (event->key() == Qt::Key_A) _joy.axisRelease(0);
    else if (event->key() == Qt::Key_D) _joy.axisRelease(0);
    else if (event->key() == Qt::Key_W) _joy.axisRelease(1);
    else if (event->key() == Qt::Key_S) _joy.axisRelease(1);
    
    this->update();
}

void MainWindow::read(QString path)
{
    QDir dir(path); 
    _sT.read(dir.filePath("servo.opts"));
}

void MainWindow::write(QString path)
{
    QDir dir(path);    
    _sT.write(dir.filePath("servo.opts"));
}

void MainWindow::joyChanged()
{
    int sel = _joy.current();
    
    QVector< XJoystick::Info > V(_joy.available());
    bool found = false;
    int i = 0;
    while (i < V.size() and not found) { found = V[i].ID == sel; ++i; }
    if (not found) {
        if (V.size() > 0) {
            _joy.select(V[0].ID);
            ui->line->hide();
            
            // Showing axis
            ui->joyAxis->show();
            
            // Showing buttons
            for (QLabel *l : _buts) l->hide();
            ui->joyButs->show();
            int n = _joy.buttonCount();
            for (int i = 0; i < n; ++i) _buts[i]->show();
        }
        else {
            _joy.select(-1);
            ui->joyAxis->hide();
            ui->joyButs->hide();
            ui->line->hide();
        }
    }
    emit joystickChanged();
}


void MainWindow::on_actionOptions_triggered()
{
    _sT.pause();
    
    OptionsWindow o(_joy, &_sT, this);
    
    connect(this, SIGNAL(joystickChanged()), &o, SLOT(joystickChanged()));
    
    if (o.exec()) {
        o.storeData();
        this->write();
    }
}

void MainWindow::on_actionImport_triggered()
{
    QFileDialog fD(this);
    fD.setFileMode(QFileDialog::ExistingFile);
    fD.setViewMode(QFileDialog::Detail);
    fD.setAcceptMode(QFileDialog::AcceptOpen);
    fD.setNameFilter(tr("Dominoes file (*df)"));
    fD.setDirectory(QDir::home());
    
    if (fD.exec()) {
        QString file;
        fD.fileSelected(file);
        
        qDebug() << "Accepted" << file;
    }
    else qDebug() << "Rejected";
}

void MainWindow::on_start_clicked()
{
    QString text = ui->start->text();
    
    if (text == "Start") {
        _sT.wakeUp();
        ui->start->setText("Stop");
    }
    else if (text == "Stop") {
        _sT.pause();
        ui->start->setText("Start");
    }
}

void MainWindow::statusBar(QString s)
{
    ui->statusbar->showMessage(s, 1500);
}

void MainWindow::update()
{
    // Joystick values
    _joy.update();
    for (int i = 0; i < XJoystick::AxisCount; ++i) {
        float temp = _joy[i];
        _axisV[i] = temp;
        _axis[i]->setText(QString::number(temp));
    }
    for (int i = 0; i < XJoystick::ButtonCount; ++i) {
        bool temp = _joy.button(i);
        _butsV[i] = temp;
        _buts[i]->setEnabled(temp);
    }
    
    _sT.setData(_axisV, _butsV);
    
    QVector<ServoThread::Servo> servo(_sT.getServosInfo());
    
    // Updating position sliders
    ui->servo0S->setValue(servo[0].pos);
    ui->servo1S->setValue(servo[1].pos);
    ui->servo2S->setValue(servo[2].pos);
    
    // Updating position labels
    ui->servo0->setText(QString::number(servo[0].pos));
    ui->servo1->setText(QString::number(servo[1].pos));
    ui->servo2->setText(QString::number(servo[2].pos));
    
    // Updating load labels
    ui->servo0L->setText(QString::number(servo[0].load));
    ui->servo1L->setText(QString::number(servo[1].load));
    ui->servo2L->setText(QString::number(servo[2].load));
    
}
