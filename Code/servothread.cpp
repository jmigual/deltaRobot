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
    _sSpeed(100),
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
        emit statusBar("Cannot read stored data", 2000);
        return;
    }
    QDataStream df(&f);
    
    QMutexLocker mL(&_mutex);
    
    int version;
    df >> version;
    if (version != Version::v_1_0) {
        emit statusBar("Error opening file", 2000);
        return;
    }
    
    df >> _cBaud >> _cPort >> _sBaud >> _sPort >> _sSpeed;
    
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
        emit statusBar("Error opening file", 2000);
        return;
    }
    
    QTextStream pF(&f);
    
    int size;
    pF >> size;
    QVector<Dominoe> temp(size);
    for (Dominoe &d : temp) pF >> d.X >> d.Y >> d.ori;
    std::sort(temp.begin(), temp.end());
    
    _mutex.lock();
    double sep = 0.6; // 0.5cm of separation
    QVector2D ori(posStart.toVector2D());
    
    _dominoe.clear();
    for (int i = 0; i < temp.size(); ++i) {
        QVector2D aux(temp[i].X, temp[i].Y);
        
        // Checking if its a vàlid position
        QVector4D aux2(aux);
        aux2[2] = workHeigh;
        if (not this->isPosAvailable(aux2)) continue;
        
        double angle = temp[i].ori + 60.0;
        if (angle >= 180.0) angle -= 180.0;
        else if (angle >= 360.0) angle -= 360.0;
        
        // Calculating direction vector
        aux -= ori;
        int steps = aux.length()/sep; 
        
        QVector<Dominoe> V(steps + 1);
        
        // Initial point
        V[0] = Dominoe(ori, angle);
        
        // Adding intermediate positions
        for (int j = 1; j <= steps; ++j){                
            Dominoe dAux(j*aux/double(steps) + ori, angle);
            V[j] = dAux;
        }
        _dominoe.push_back(V);
    }
    _dChanged = true;
    _mutex.unlock();
    
    f.close();
    
    emit statusBar("File loaded succesfully", 1000);
}

void ServoThread::setData(QVector<float> &aV, QVector<bool> &buts)
{
    _mutex.lock();
    // Copying the joystick values
    _axis = QVector4D(aV[0], aV[1], aV[2], aV[3]);
    _axis.normalize();
    _axis[3] *= 5;
    for (int i = 0; i < buts.size(); ++i) _buts[i] |= buts[i];    
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
       << _servos.size();    
    for (const Servo &s : _servos) df << s.ID;
    
    _mutex.unlock();
}

bool ServoThread::isPosAvailable(const QVector4D &newPos)
{    
    if (newPos.toVector2D().lengthSquared() > workRadSq) return false;
    if (newPos.z() > workHeigh + 0.7) return false;
    
    QVector<double> D(4);
    this->setAngles(newPos, D);
    
    for (int i = 0; i < 3; ++i) {
        if (qIsNaN(D[i])) return false;
        if (D[i] > maxAngle or D[i] < minAngle) return false;
    }
    
    return true;
}

bool ServoThread::isReady(const QVector<double> &S, 
                          const QVector4D &pos, double err)
{
    QVector<double> D(4);
    this->setAngles(pos, D);
    
    for (int i = 0; i < 3; ++i) if (abs(S[i] - D[i]) > err) return false;
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
    
    // Contains the servos ID
    QVector< int > ID(_sNum);
    
    // Contains the current servo data
    QVector< double > S(_sNum);
    
    // Contains the servos angles
    QVector<double> D(4);
    D[3] = 150.0;
    
    // First initialization
    _mutex.lock();
    for (int i = 0; i < A.size(); ++i) {
        A[i] = AX12(&dxl);  
        A[i].setID(_servos[i].ID);
        ID[i] = _servos[i].ID;
        A[i].setSpeed(_sSpeed);
        A[i].setComplianceSlope(ccwCS, cwCS);
    }
    _mutex.unlock();
    
    QVector4D pos(posIdle);
    QVector4D axis(0, 0, 0, 0);
    QVector< bool > buts;
    
    // Contains the domino number to put
    int dom = 0;
    int pas = 0;
    double speed = 100.0;
    QVector< QVector< Dominoe > > Dom;
    
    // Main while
    while (not _end) {
        
        // Pause
        _mutex.lock();
        if (not _end and _pause) {
            dxl.terminate();
            
            // Thread pause
            _cond.wait(&_mutex);
            
            if (_end) exit(0);
            dxl.initialize(sPort, sBaud);
        }
        _mutex.unlock();
        
        // Get current servo position
        for (int i = 0; i < 3; ++i) S[i] = A[i].getCurrentPos();
        if (_mod == Mode::Manual) S[3] = A[3].getCurrentPos();
        
        
        /*********** MUTEX ***********/
        // Handling changes of data
        _mutex.lock();
        if (_dChanged) {
            if (sPort != _sPort or sBaud != _sBaud) {
                sPort = _sPort;
                sBaud = _sBaud;
                dxl.terminate();
                dxl.initialize(sPort, sBaud);
            }
            for (int i = 0; i < S.size(); ++i) {
                A[i].setID(_servos[i].ID);
                ID[i] = _servos[i].ID;
                A[i].setSpeed(_sSpeed);
                A[i].setComplianceSlope(ccwCS, cwCS);
            }
            
            speed = _sSpeed;
            Dom = _dominoe;
            dom = 0;
            pas = 0;
            pos = posIdle;            
            this->setAngles(pos, D);
            this->setGoalPosition(ID, D, dxl);
            _dChanged = false;
        }

        // Joystick and buttons update, must use mutex
        for (int i = 0; i < _sNum; ++i) _servos[i].pos =  S[i];
        axis = _axis;
        buts = _buts;
        for (bool &b : _buts) b = 0;
        _pos = pos;
        _mutex.unlock();
        
        
        /******** MODE ********/
        // Main function with data updated
        
        ////// MANUAL //////
        if (_mod == Mode::Manual) {
            QVector4D posAux = pos + 0.5*axis;
            if (posAux[3] < 0) posAux[3] = 0;
            if (posAux[3] > 300.0) posAux[3] = 300.0;
            
            bool ok = this->isPosAvailable(posAux);
            ok &= this->isReady(S, pos, maxErr + 5.0);
            if (ok) pos = posAux;    
        } 
        ////// CONTROLLED //////
        else if (_mod == Mode::Controlled) {
            switch(_status) {
            case Status::begin:
                for (AX12 &a : A) a.setSpeed(speed/10.0);
                pos = posStart;
                if (this->isReady(S, pos, maxErr))  {
                    _status = Status::take;
                    QThread::msleep(500);
                }
                break;
                
            case Status::take:
                pos[2] = workHeigh;
                if (this->isReady(S, pos, maxErr)) {
                    for (AX12 &a : A) a.setSpeed(speed);
                    emit statusBar("Esperant peça", -1);
                    _status = Status::waiting;
                }
                break;
                
            case Status::waiting:
                if (buts[0]) {
                    pas = 0;
                    _status = Status::rotate;
                    emit statusBar("Girant!", -1);
                    
                    for (AX12 &a : A) a.setSpeed(speed/3.5);
                }
                else break;
                
            case Status::rotate:
            {
                S[3] = A[3].getCurrentPos();
                pos[3] = Dom[dom][0].ori;
                double aux = abs(S[3] - Dom[dom][0].ori);
                if (aux < maxErr) {
                    _status = Status::going;
                    QThread::msleep(1000);
                    emit statusBar("Posicionant", -1);
                }
            }
                break;
                
            case Status::going:
            {
                Dominoe &domi = Dom[dom][pas];
                pos = QVector4D (domi.X, domi.Y, workHeigh, domi.ori);
                if (pos.x() < 8.0) pos[2] = workHeigh + 0.3;
                if (pos.x() < 7.5) pos[2] = workHeigh + 0.5;
                if (pos.x() < 7.0) pos[2] = workHeigh + 0.6;
                if (pos.x() < 2.0) pos[2] = workHeigh + 0.3;
                double err;
                pas == Dom[dom].size() - 1 ? err = maxErr : err = 2*maxErr;
                
                if (this->isReady(S, pos, err)) {
                    ++pas;
                    if (pas == Dom[dom].size()) {
                        pas = 0;
                        _status = Status::ending;
                        QThread::msleep(200);
                        emit statusBar("Col·locada", 1500);
                        
                        for (AX12 &a : A) a.setSpeed(speed);
                    }
                }
            }   
                break;
                
            case Status::ending:
                pos[2] = descHeigh[pas];
                
                if (this->isReady(S, pos, maxErr)) {
                    ++pas;
                    if (pas == 4) {
                        _status = Status::begin;
                        QThread::msleep(300);
                        if (dom == Dom.size() - 1) {
                            dom = 0;
                            pas = 0;
                            _mod = Mode::Reset;
                        }
                        else ++dom;
                    }
                }
                break;
            
            default:
                _status = Status::begin;
            
            }
        } 
        else if (_mod == Mode::Reset) {
            _mod = Mode::Manual;
            pos = posIdle;
            dom = 0;
        }
        
        this->setAngles(pos, D);
        this->setGoalPosition(ID, D, dxl);
    }
    dxl.terminate();
    exit(0);
}

void ServoThread::setAngles(const QVector4D &pos, QVector<double> &D)
{    
    double x1 = pos.x() + L2 - L1;
    double y1 = -pos.z();
    double z1 = pos.y();
    D[0] = singleAngle(x1,y1,z1);
    
    double x2 = pos.y()*sin60 - pos.x()*cos60 + L2 - L1;
    double y2 = -pos.z();
    double z2 = -pos.y()*cos60 - pos.x()*sin60;
    D[1] = singleAngle(x2,y2,z2);
    
    double x3 = -pos.y()*sin60 - pos.x()*cos60 + L2 - L1;
    double y3 = -pos.z();
    double z3 = -pos.y()*cos60 + pos.x()*sin60;
    D[2] = singleAngle(x3,y3,z3);
    
    for (double &d : D) d = 150.0 - d*180/M_PI;
    D[3] = pos.w();
}

void ServoThread::setGoalPosition(const QVector<int> &ID, 
                                  const QVector<double> &pos, dynamixel &dxl)
{
    dxl.set_txpacket_id(BROADCAST_ID);
    dxl.set_txpacket_instruction(INST_SYNC_WRITE);
    dxl.set_txpacket_parameter(0, AX12::RAM::GoalPosition);
    dxl.set_txpacket_parameter(1, 2);
    
    Q_ASSERT(ID.size() == pos.size());
    for (int i = 0; i < ID.size(); ++i) {
        unsigned int data = (pos[i]/300.0)*1023.0;
        
        dxl.set_txpacket_parameter(2 + 3*i, ID[i]);
        dxl.set_txpacket_parameter(2 + 3*i + 1, LOBYTE(data));
        dxl.set_txpacket_parameter(2 + 3*i + 2, HIBYTE(data));
    }
    dxl.set_txpacket_length(4 + 3*ID.size());
    dxl.txrx_packet();
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

