#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "chartwindow.h"

#include <QMainWindow>
#include <pigpio.h>
#include <QVector>
#include <QtGui>

// General defines
#define BITS_IN_BYTE 8
#define BITS_IN_ADDRESS 7

// Transaction defines
#define MAX_BYTES_PER_TRANSACTION 8
#define NUM_IDLE_PULSES 1
#define NUM_CTRL_PULSES 7
#define NUM_DATA_PULSES (BITS_IN_BYTE * MAX_BYTES_PER_TRANSACTION)
#define MAX_PULSES ((NUM_IDLE_PULSES + NUM_CTRL_PULSES + NUM_DATA_PULSES)*2) // each pulse is actually two
#define TRANSMISSION_HZ 2500

#define DUTY_PERCENT_HIGH 75
#define DUTY_PERCENT_LOW 25
#define DUTY_PERCENT_IDLE 50

// HW defines
#define GPIO_TX_PIN 4
#define GPIO_RX_PIN 12
#define GPIO_RD_EN_PIN 17
#define GPIO_WRT_EN_PIN 17
#define GPIO_FUTURE 1

namespace Ui {
class MainWindow;
}
// A bit is represented as two pulses
typedef struct
{
    gpioPulse_t firstPulse;
    gpioPulse_t secondPulse;
} gpioBit_t;

// Read data from GPIO is encoded in the following vectors
typedef struct
{
    QVector<unsigned> *rising;
    QVector<unsigned> *falling;
} waveform_t;

// The RW bit can be either of these values
typedef enum
{
    READ,
    WRITE
} rw_enumType;

class MainWindow : public QMainWindow
{
    Q_OBJECT


public:
    explicit MainWindow(ChartWindow *w, QWidget *parent = 0);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    // Seperate chart window
    ChartWindow *cw;

    // Build a pulse representing one bit
    gpioBit_t buildPulseBit(unsigned duty, unsigned hz, unsigned gpioPin);
    // Build pulse train representing all data
    unsigned buildPulses(rw_enumType cmd, gpioPulse_t *destPulse, unsigned gpioPin, unsigned address, unsigned numDataBytes, unsigned *dataBytes);
    // Show chart with read data
    void buildAndShowChart(QVector<unsigned> riseTimes, QVector<unsigned> fallTimes, unsigned periodInUS, QVector<unsigned> bits);

private slots:
    // Encode and write the data from the GUI to the GPIO
    void on_Write_clicked();
    // Encode and write 'read' data onto the GPIO and analyze the result
    void on_Read_clicked();
    // Set GPIO on/off
    void on_checkBox_clicked();
};

#endif // MAINWINDOW_H
