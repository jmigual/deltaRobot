/// @file servothread.cpp Contains the ServoThread class implementation
#include "servothread.h"

ServoThread::ServoThread() :
    _axis(0, 0, 0),
    _buts(XJoystick::ButtonCount),
    _cBaud(9600),
    _cPort("COM3"),
    _dChanged(true),
    _end(false),
    _mod(Mode::manual),
    _pause(true),
    _sBaud(1000000),
    _servos(_sNum),
    _sPort("COM9"),
    _sPortChanged(false),
    _sSpeed(30)
{
    for (Servo &s : _servos) s.ID = -1;
}

ServoThread::~ServoThread()
{
    _mutex.lock();
    _end = true;
    _cond.wakeOne();
    _mutex.unlock();
    
    wait();
}

void ServoThread::read(QString file)
{
    // Opening file for reading
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        emit statusBar("Cannot read stored data");
        return;
    }
    QDataStream df(&f);
    
    QMutexLocker mL(&_mutex);
    
    int version;
    df >> version;
    if (version != Version::v_1_0) {
        emit statusBar("Error opening file");
        return;
    }
    
    df >> _cBaud >> _cPort >> _sBaud >> _sPort >> _sSpeed;
    unsigned int en;
    df >> en;
    _mod = static_cast<Mode>(en);
    
    int size;
    df >> size;
    _servos.resize(size);
    for (Servo &s : _servos) df >> s.ID;
    _dChanged = true;
    
}

void ServoThread::readPath(QString file)
{
    // Opening file for reading
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        emit statusBar("Error opening file");
        return;
    }
    
    QTextStream pF(&f);
    
    int size;
    pF >> size;
    _mutex.lock();
    _dominoe.resize(size);
    for (Dominoe &d : _dominoe) pF >> d.X >> d.Y >> d.ori;
    _mutex.unlock();
    
    emit statusBar("File loaded succesfully");
}

void ServoThread::setData(QVector<float> &aV, QVector<bool> &buts)
{
    _mutex.lock();
    // Copying the joystick values
    _axis = QVector3D(aV[0], aV[1], aV[2]);
    _axis.normalize();
    _buts = buts;    
    _mutex.unlock();
}

void ServoThread::write(QString file)
{
    // Opening file for writing
    QFile f(file);
    f.open(QIODevice::WriteOnly);
    QDataStream df(&f);
    
    _mutex.lock();
    
    // Clamp and servos baud rate and port must be writen
    df << int(Version::v_1_0) << _cBaud << _cPort << _sBaud << _sPort << _sSpeed
       << int(_mod) << _servos.size();    
    for (const Servo &s : _servos) df << s.ID;
    
    _mutex.unlock();
}

void ServoThread::run()
{
    // First initializations
    _mutex.lock();
    int sBaud = _sBaud;
    QString sPort = _sPort;
    _mutex.unlock();
    
    // Serial port interface
    dynamixel dxl(sPort, sBaud);
    
    // Contains the servos comunication
    QVector< AX12 > A(_sNum);    
    
    // Contains the servos angles
    QVector<double> D(3);
    
    // First initialization
    _mutex.lock();
    for (int i = 0; i < A.size(); ++i) {
        A[i] = AX12(&dxl);  
        A[i].setID(_servos[i].ID);
    }
    _mutex.unlock();
    
    // Contains the current servo data
    QVector< Servo > S(_sNum);
    
    QVector3D pos(0, -20, 0);
    QVector3D axis(0, 0, 0);
    
    // Contains the domino number to put
    double dom = 0;
    
    while (not _end) {
        msleep(10);
        _mutex.lock();
        
        // Pause
        if (not _end and _pause) {
            dxl.terminate();
            _cond.wait(&_mutex);
            dxl.initialize(sPort, sBaud);
        }       
        
        // Data changed handle
        if (_dChanged) {
            if (sPort != _sPort or sBaud != _sBaud) {
                sPort = _sPort;
                sBaud = _sBaud;
                dxl.terminate();
                dxl.initialize(sPort, sBaud);
            }
            
            for (int i = 0; i < S.size(); ++i) {
                A[i].setID(_servos[i].ID);
                A[i].setSpeed(_sSpeed);
            }
            
            
            pos = QVector3D(0, -20, 0);            
            
            setAngles(pos, D);
            for (int i = 0; i < 3; ++i) {
                double d = 240 + D[i]*180/M_PI;
                A[i].setGoalPosition(d);
            }
            _dChanged = false;
        }

        for (int i = 0; i < A.size(); ++i) {
            S[i].load = A[i].getCurrentLoad();
            S[i].pos = A[i].getCurrentPos();
        }
        _servos = S;
        _mutex.unlock();
        
        
        // Main function with data updated
        if (_mod == Mode::manual) {
            for (int i = 0; i < 3; ++i) {
                double diff = abs(S[i].pos - D[i]);
                if (diff < 0.5) {
                    pos += 3*axis;
                }
            }
        } else if (_mod == Mode::controlled) {
            ++dom;
        }
        qDebug() << pos.x() << pos.y() << pos.z();
        this->setAngles(pos, D);
        for (int i = 0; i < 3; ++i) A[i].setGoalPosition(D[i]);
    }
    dxl.terminate();
    exit(0);
}

void ServoThread::setAngles(const QVector3D &pos, QVector<double> &D)
{    
    double x1 = pos.x() + L2 - L1;
    double y1 = pos.y();
    double z1 = pos.z();
    D[0] = singleAngle(x1,y1,z1);
    
    double x2 = pos.z()*sin60 - pos.x()*cos60 + L2 - L1;
    double y2 = pos.y();
    double z2 = -pos.z()*cos60 - pos.x()*sin60;
    D[1] = singleAngle(x2,y2,z2);
    
    double x3 = -pos.z()*sin60 - pos.x()*cos60 + L2 - L1;
    double y3 = pos.y();
    double z3 = -pos.z()*cos60 + pos.x()*sin60;
    D[2] = singleAngle(x3,y3,z3);
}

double ServoThread::singleAngle(double x0, double y0, double z0)
{
    double n = b*b - a*a - z0*z0 - x0*x0 - y0*y0;
    double raiz = sqrt (n*n*y0*y0 - 4*(x0*x0 + y0*y0)*(-x0*x0*a*a + n*n/4));
    
    if (x0 < 0) raiz *= -1;
    double y = (-n*y0 + raiz ) / (2*(x0*x0 + y0*y0));
    
    int signe = 1;
    if ((b*b - (y0 + a)*(y0 + a)) < (x0*x0 + z0*z0) && x0 < 0) signe *= -1;
    double x = sqrt(a*a - y*y)*signe;
    return atan2 (y,x);
}

