/// @file servothread.h Contains the ServoThread class declaration
#ifndef SERVOTHREAD_H
#define SERVOTHREAD_H

// Qt Libraries
#include <QDebug>
#include <QDir>
#include <QMutex>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

// Other libraries
#include <xjoystick.h>

// User libraries
#include "dxl/ax12.h"

/// The ServoThread's class handles the comunication between the delta robot
/// servos and the PC.
class ServoThread : public QThread
{
    Q_OBJECT
    
    enum Version 
    {
        v_1_0
    };
    
public:
    
    /// Struct for the AX12 servos
    struct Servo 
    {
        int ID;         ///< Contains the servo ID
        double load;    ///< Contains the servo load
        double pos;     ///< Contains the servo position
        
        /// Default constructor
        Servo(int ID = -1, double load = -1, double pos = -1) 
            : ID(ID), load(load), pos(pos) {}
        
        /// Copy constructor
        Servo(Servo &s) : ID(s.ID), load(s.load), pos(s.pos) {}
    };
    
    /// Contains the working mode
    enum Mode 
    {
        controlled,
        manual
    };
    
    /// Default constructor
    ServoThread();
    
    /// Default destructor
    ~ServoThread();
    
    /// Continues program's execution
    inline void cont()
    {
        _mutex.lock();
        _pause = false;
        _cond.wakeOne();
        _mutex.unlock();
    }
    
    /// Ends the execution
    inline void end()
    {
        _mutex.lock();
        _end = true;
        _cond.wakeOne();
        _mutex.unlock();
        
        wait();
    }
    
    /// Loads the data from the selected file
    void load(QString &file);
    
    /// Pauses the execution
    inline void pause()
    {
        _mutex.lock();
        _pause = true;
        _mutex.unlock();
    }
    
    /// Returns the current servo Baud rate
    inline int getServoBaud()
    {
        QMutexLocker mL(&_mutex);
        return _sBaud;
    }
    
    /// Returns the current servo Port
    inline QString getServoPort()
    {
        QMutexLocker mL(&_mutex);
        return _sPort;
    }
    
    /// Returns both servo Port and baud Rate
    inline void getServoPortInfo(QString &port, int &baud)
    {
        _mutex.lock();
        baud = _sBaud;
        port = _sPort;
        _mutex.unlock();
    }
    
    /// Adds the loaded data
    /// @param aV Contains the axis values
    /// @param buts Contains the buttons values
    void setData(QVector< float > &aV, QVector<bool> &buts);
    
    /// Sets the servos ID
    inline void setSID (QVector< int > &V) 
    {
        _mutex.lock();
        if (V.size() != _servos.size()) _servos.resize(V.size());
        
        for (int i = 0; i < V.size(); ++i) _servos[i].ID = V[i];
        _dChanged = true;
        _mutex.unlock();
    }
    
    /// Writes data to the selected directory
    /// @param file Path to the file
    void write(QString &file); 
    
private:
    
    /// Contains the axis value
    QVector < float > _axis;
    
    /// Contains the buttons value
    QVector < bool > _buts;
    
    /// Contains the baud rate used to comunicate with the clamp
    int _cBaud;
    
    /// To start and pause the thread
    QWaitCondition _cond;
        
    /// Contains the selected com port used to comunitate with the clamp
    QString _cPort;
    
    /// True if the data changes
    bool _dChanged;
    
    /// True when we must end executino
    bool _end;
    
    /// Contains the working mode
    Mode _mod;
    
    /// To prevent memory errors
    QMutex _mutex;
    
    /// Pauses the execution of the thread
    bool _pause;
    
    /// Contains the used baud rate to comunicate with the servos
    int _sBaud;
    
    /// Contains the servos information
    QVector< Servo > _servos;
    
    /// Contains the selected com port used in the comunication with servos
    QString _sPort;
    
    /// True if the servos port changes
    bool _sPortChanged;
    
    /// Used to create another thread
    void run();
    
};

#endif // SERVOTHREAD_H
