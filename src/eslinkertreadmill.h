#ifndef ESLINKERTREADMILL_H
#define ESLINKERTREADMILL_H

#include <QBluetoothDeviceDiscoveryAgent>
#include <QtBluetooth/qlowenergyadvertisingdata.h>
#include <QtBluetooth/qlowenergyadvertisingparameters.h>
#include <QtBluetooth/qlowenergycharacteristic.h>
#include <QtBluetooth/qlowenergycharacteristicdata.h>
#include <QtBluetooth/qlowenergycontroller.h>
#include <QtBluetooth/qlowenergydescriptordata.h>
#include <QtBluetooth/qlowenergyservice.h>
#include <QtBluetooth/qlowenergyservicedata.h>
#include <QtCore/qbytearray.h>

#ifndef Q_OS_ANDROID
#include <QtCore/qcoreapplication.h>
#else
#include <QtGui/qguiapplication.h>
#endif
#include <QtCore/qlist.h>
#include <QtCore/qmutex.h>
#include <QtCore/qscopedpointer.h>
#include <QtCore/qtimer.h>

#include <QDateTime>
#include <QObject>

#include "treadmill.h"
#include "virtualtreadmill.h"

class eslinkertreadmill : public treadmill {
    Q_OBJECT
  public:
    eslinkertreadmill(uint32_t poolDeviceTime = 200, bool noConsole = false, bool noHeartService = false,
                      double forceInitSpeed = 0.0, double forceInitInclination = 0.0);
    bool connected();
    double odometer();

    void setLastSpeed(double speed);
    void setLastInclination(double inclination);

    void *VirtualTreadMill();
    void *VirtualDevice();

  private:
    bool sendChangeFanSpeed(uint8_t speed);
    double GetSpeedFromPacket(const QByteArray &packet);
    double GetInclinationFromPacket(const QByteArray &packet);
    double GetKcalFromPacket(const QByteArray &packet);
    double GetDistanceFromPacket(const QByteArray &packet);
    void forceSpeedOrIncline(double requestSpeed, double requestIncline);
    void updateDisplay(uint16_t elapsed);
    void btinit(bool startTape);
    void writeCharacteristic(uint8_t *data, uint8_t data_len, const QString &info, bool disable_log = false,
                             bool wait_for_response = false);
    void startDiscover();
    double DistanceCalculated = 0;
    volatile bool incompletePackets = false;
    bool noConsole = false;
    bool noHeartService = false;
    uint32_t pollDeviceTime = 200;
    bool searchStopped = false;
    double lastSpeed = 0.0;
    double lastInclination = 0;
    uint8_t sec1Update = 0;
    uint8_t firstInit = 0;
    QByteArray lastPacket;
    QDateTime lastTimeCharacteristicChanged;
    bool firstCharacteristicChanged = true;

    QTimer *refresh;
    virtualtreadmill *virtualTreadMill = nullptr;

    QLowEnergyService *gattCommunicationChannelService = nullptr;
    QLowEnergyCharacteristic gattWriteCharacteristic;
    QLowEnergyCharacteristic gattNotifyCharacteristic;

    bool initDone = false;
    bool initRequest = false;

  Q_SIGNALS:
    void disconnected();
    void debug(QString string);
    void speedChanged(double speed);
    void packetReceived();

  public slots:
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void searchingStop();

  private slots:

    void characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);
    void characteristicWritten(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);
    void descriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &newValue);
    void stateChanged(QLowEnergyService::ServiceState state);
    void controllerStateChanged(QLowEnergyController::ControllerState state);

    void serviceDiscovered(const QBluetoothUuid &gatt);
    void serviceScanDone(void);
    void update();
    void error(QLowEnergyController::Error err);
    void errorService(QLowEnergyService::ServiceError);
};

#endif // ESLINKERTREADMILL_H
