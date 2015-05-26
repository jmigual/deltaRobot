/// @file servothread.cpp Contains the ServoThread class implementation
#include "servothread.h"

ServoThread::ServoThread() :
    _axis(0, 0, 0, 0),
    _buts(XJoystick::ButtonCount),
    _cBaud(9600),
    _cPort("COM3"),
    _dChanged(true),
    _end(false),
    _mod(Mode::Manual),
    _pause(true),
    _sBaud(1000000),
    _servos(_sNum),
    _sPort("COM9"),
    _sPortChanged(false),
    _sSpeed(30),
    _status(Status::begin)
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
    QVector<Dominoe> temp(size);
    for (Dominoe &d : temp) pF >> d.X >> d.Y >> d.ori;

    _mutex.lock();
    double sep = 2; // 2cm of separation
    QVector2D ori(12, 0);
    
    for (int i = 0; i < temp.size(); ++i) {
        QVector2D aux(temp[i].X, temp[i].Y);
        aux -= ori;
        int steps = aux.length()/sep; 
      
        for (int j = 1; j <= steps; ++j){                
            Dominoe dAux(j*aux/double(steps) + ori, temp[i].ori);
            _dominoe[i].push_back(dAux);         
        }
    }
    _dChanged = true;
    _mutex.unlock();
    
    f.close();
    
    emit statusBar("File loaded succesfully");
}

void ServoThread::setData(QVector<float> &aV, QVector<bool> &buts)
{
    _mutex.lock();
    // Copying the joystick values
    _axis = QVector4D(aV[0], aV[1], aV[2], aV[3]);
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

bool ServoThread::isPosAvailable(const QVector<ServoThread::Servo> &S, 
                                 const QVector<double> &D, 
                                 const QVector3D &newPos, double err)
{
    for (int i = 0; i < 3; ++i) {
        double aux = abs(S[i].pos - D[i]);
        if (aux > err) return false;
    }
    
    if (newPos.toVector2D().lengthSquared() > workRadSq) return false;
    
    QVector<double> theta(3);
    this->setAngles(newPos, theta);
    
    for (const double &d : theta) {
        if (qIsNaN(d)) return false;
        else if (d > maxAngle or d < minAngle) return false;
    }
    
    return true;
}

bool ServoThread::isReady(const QVector<ServoThread::Servo> &S, 
                          const QVector3D &pos, double err)
{
    QVector<double> D(3);
    this->setAngles(pos, D);
    
    for (int i = 0; i < 3; ++i) {
        double aux = abs(S[i].pos - D[i]);
        if (aux > err) return false;
    }
    return true;
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
    QVector<AX12> A(4);
    
    // Contains the servos angles
    QVector<double> D(3);
    // First initialization
    _mutex.lock();
    for (int i = 0; i < A.size(); ++i) {
        A[i] = AX12(&dxl);  
        A[i].setID(_servos[i].ID);
        A[i].setSpeed(_sSpeed);
        A[i].setComplianceSlope(ccwCS, cwCS);
    }
    _mutex.unlock();
    
    // Contains the current servo data
    QVector< Servo > S(_sNum);
    
    QVector4D pos(posIdle);
    QVector4D axis(0, 0, 0, 0);
    QVector< bool > buts;
    
    // Contains the domino number to put
    unsigned int dom = 0;
    unsigned int pas = 0;
    QVector< QVector< Dominoe > > Dom;
    
    while (not _end) {
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
                A[i].setComplianceSlope(ccwCS, cwCS);
            }
            
            Dom = _dominoe;
            dom = 0;
            
            pos = posIdle;            
            this->setAngles(pos.toVector3D(), D);
            for (int i = 0; i < 3; ++i) A[i].setGoalPosition(D[i]);
            
            _dChanged = false;
        }

        for (int i = 0; i < A.size(); ++i) {
            _servos[i].pos = S[i].pos = A[i].getCurrentPos();
        }
        axis = _axis;
        buts = _buts;
        _pos = pos;
        _mutex.unlock();
        
        
        // Main function with data updated
        if (_mod == Mode::Manual) {
            QVector4D posAux = pos + 0.5*axis;
            
            bool ok = this->isPosAvailable(S, D, posAux.toVector3D(), 
                                           maxErr + 4);
            if (ok) pos = posAux;            
        } 
        else if (_mod == Mode::Controlled) {
            switch(_status) {
            case Status::begin:
                pos = posStart;
                if (this->isReady(S, pos.toVector3D(), maxErr)) 
                    _status = Status::take;                
                break;
                
            case Status::take:
                pos[2] = workHeigh;
                if (this->isReady(S, pos.toVector3D(), maxErr)) 
                    _status = Status::waiting;
                break;
                
            case Status::waiting:
                if (buts[0]) _status = Status::rotate;
                else break;
                
            case Status::rotate:
            {
                double angle = Dom[dom][0].ori;
                angle += 150.0;
                if (angle > 180.0) angle -= 180.0;
                A[3].setGoalPosition(angle);
                double aux = abs(S[3].pos - angle);
                if (aux < maxErr) {
                    _status = Status::going;
                    pas = 0;
                }
            }
                break;
                
            case Status::going:
                
                break;
                
            case Status::ending:
                
                break;
            
            default:
                _status = Status::begin;
            
            }
        } 
        else if (_mod == Mode::Reset) {
            _mod = Mode::Manual;
            pos = QVector3D(0, 0, -20);
            dom = 0;
        }
        
        this->setAngles(pos.toVector3D(), D);
        for (int i = 0; i < 3; ++i) A[i].setGoalPosition(D[i]);
        A[3].setGoalPosition(pos.w());
    }
    dxl.terminate();
    exit(0);
}

void ServoThread::setAngles(const QVector3D &pos, QVector<double> &D)
{    
    double x1 = pos.x() + L2 - L1;
    double y1 = pos.z();
    double z1 = pos.y();
    D[0] = singleAngle(x1,y1,z1);
    
    double x2 = pos.y()*sin60 - pos.x()*cos60 + L2 - L1;
    double y2 = pos.z();
    double z2 = -pos.y()*cos60 - pos.x()*sin60;
    D[1] = singleAngle(x2,y2,z2);
    
    double x3 = -pos.y()*sin60 - pos.x()*cos60 + L2 - L1;
    double y3 = pos.z();
    double z3 = -pos.y()*cos60 + pos.x()*sin60;
    D[2] = singleAngle(x3,y3,z3);
    
    for (double &d : D) d = 240 + d*180/M_PI;
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

