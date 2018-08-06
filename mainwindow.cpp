#include <stdarg.h>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>

#include <unistd.h>

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtCharts/QChartView>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLineSeries>

static void _cb(int gpio, int level, uint32_t tick, void *user)
{
    switch (level)
    {
    case 0:
        (*(waveform_t*)user).falling->append(tick);
        break;
    case 1:
        (*(waveform_t*)user).rising->append(tick);
        break;
    }

}

static gpioBit_t BuildBit(unsigned duty, unsigned hz, unsigned gpioPin)
{
    gpioBit_t retVal;

    // Each bit has two pulses
    gpioPulse_t pulse1, pulse2;

    unsigned periodInUS = (1/(float)hz) * 1000000;
    unsigned onTimeInUS = periodInUS/(100/(float)duty);
    unsigned offTimeInUS = periodInUS - onTimeInUS;

    pulse1.gpioOn = (1<<gpioPin);
    pulse1.gpioOff = 0;
    pulse1.usDelay = onTimeInUS;

    pulse2.gpioOn = 0;
    pulse2.gpioOff = (1<<gpioPin);
    pulse2.usDelay = offTimeInUS;

    retVal.firstPulse = pulse1;
    retVal.secondPulse = pulse2;

    return retVal;
}
#define TRANSMISSION_HZ 2500
#define DUTY_PERCENT_HIGH 75
#define DUTY_PERCENT_LOW 25
#define DUTY_PERCENT_IDLE 50
#define BITS_IN_ADDRESS 7
#define BITS_IN_BYTE 8
static unsigned BuildPulses(rw_enumType cmd, gpioPulse_t *destPulse, unsigned gpioPin, unsigned address, unsigned numDataBytes, unsigned *dataBytes)
{

    unsigned numPulses = 0;

    if (numDataBytes > 8)
        return 0;

    /* --- Build idle pulse ---*/
    destPulse[numPulses++] = BuildBit(DUTY_PERCENT_IDLE, TRANSMISSION_HZ, gpioPin).firstPulse;
    destPulse[numPulses++] = BuildBit(DUTY_PERCENT_IDLE, TRANSMISSION_HZ, gpioPin).secondPulse;

    /* --- Build wrote pulse (bit is zero) ---*/
    if (cmd == WRITE)
    {
        destPulse[numPulses++] = BuildBit(DUTY_PERCENT_LOW, TRANSMISSION_HZ, gpioPin).firstPulse;
        destPulse[numPulses++] = BuildBit(DUTY_PERCENT_LOW, TRANSMISSION_HZ, gpioPin).secondPulse;
    }
    else
    {
        destPulse[numPulses++] = BuildBit(DUTY_PERCENT_HIGH, TRANSMISSION_HZ, gpioPin).firstPulse;
        destPulse[numPulses++] = BuildBit(DUTY_PERCENT_HIGH, TRANSMISSION_HZ, gpioPin).secondPulse;

    }

    /* --- Build address pulse ---*/
    for (unsigned i = 1<<(BITS_IN_ADDRESS-1); i != 0; i>>=1)
    {
        // If bit is z one, encode a 1
        if (address & i)
        {
            // Build One-pulse (bit is zero)
            destPulse[numPulses++] = BuildBit(DUTY_PERCENT_HIGH, TRANSMISSION_HZ, gpioPin).firstPulse;
            destPulse[numPulses++] = BuildBit(DUTY_PERCENT_HIGH, TRANSMISSION_HZ, gpioPin).secondPulse;
        }

        // If bit is a zero, encode a zero
        else
        {
            // Build One-pulse (bit is zero)
            destPulse[numPulses++] = BuildBit(DUTY_PERCENT_LOW, TRANSMISSION_HZ, gpioPin).firstPulse;
            destPulse[numPulses++] = BuildBit(DUTY_PERCENT_LOW, TRANSMISSION_HZ, gpioPin).secondPulse;
        }
    }

    /* --- Build data pulse ---*/
    // For each byte in address
    for (unsigned i = 0; i < numDataBytes; i++)
    {
        // Build address pulse
            for (unsigned j = 1<<(BITS_IN_BYTE-1); j != 0; j>>=1)
        {
            // If bit is z one, encode a 1
            if (dataBytes[i] & j)
            {
                // Build One-pulse (bit is zero)
                destPulse[numPulses++] = BuildBit(DUTY_PERCENT_HIGH, TRANSMISSION_HZ, gpioPin).firstPulse;
                destPulse[numPulses++] = BuildBit(DUTY_PERCENT_HIGH, TRANSMISSION_HZ, gpioPin).secondPulse;
            }

            // If bit is a zero, encode a zero
            else
            {
                // Build One-pulse (bit is zero)
                destPulse[numPulses++] = BuildBit(DUTY_PERCENT_LOW, TRANSMISSION_HZ, gpioPin).firstPulse;
                destPulse[numPulses++] = BuildBit(DUTY_PERCENT_LOW, TRANSMISSION_HZ, gpioPin).secondPulse;
            }
        }
    }
    /* --- Build idle pulse ---*/
    destPulse[numPulses++] = BuildBit(DUTY_PERCENT_IDLE, TRANSMISSION_HZ, gpioPin).firstPulse;
    destPulse[numPulses++] = BuildBit(DUTY_PERCENT_IDLE, TRANSMISSION_HZ, gpioPin).secondPulse;

    return numPulses;
}


MainWindow::MainWindow(ChartWindow *w, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    cw = w;
}

MainWindow::~MainWindow()
{
    delete ui;
}

#define MAX_BYTES_PER_TRANSACTION 8
#define BITS_PER_BYTE 8
#define NUM_IDLE_PULSES 1
#define NUM_CTRL_PULSES 7
#define NUM_DATA_PULSES (BITS_PER_BYTE * MAX_BYTES_PER_TRANSACTION)
#define MAX_PULSES ((NUM_IDLE_PULSES + NUM_CTRL_PULSES + NUM_DATA_PULSES)*2) // each pulse is actually two
#define GPIO_TX_PIN 4
#define GPIO_RX_PIN 12
#define GPIO_FUTURE 1
void MainWindow::on_Write_clicked()
{
    gpioPulse_t pulse[MAX_PULSES];

    if (gpioInitialise() < 0)
    {
        QMessageBox::information(this, "Error", "Cant init GPIO. Are you root?");
        return;
    }

    // Check the address
    bool ok;
    unsigned char address = ui->write_address->text().toUInt(&ok, 16);
    if (!ok || address > 0x7F)
    {
        QMessageBox::information(this, "Error", "Address is not valid 7-bit hex value");
        return;
    }

    // Check the Data
    if (ui->write_data->text().length() %2 != 0)
    {
        QMessageBox::information(this, "Error", "Data contains partial byte. You should probably not do that.");
        return;
    }
    unsigned data[MAX_BYTES_PER_TRANSACTION];
    int dataIdx = 0;
    for (int i = 0; i < ui->write_data->text().length(); i+=2)
    {
        QString temp = ui->write_data->text().mid(i, 2);
        data[dataIdx++] = temp.toUInt(&ok, 16);
    }

    unsigned numPulses = BuildPulses(WRITE, pulse, GPIO_TX_PIN, address, dataIdx, data);
    gpioSetMode(GPIO_TX_PIN, PI_OUTPUT);

    int wave_id;

    gpioWrite(GPIO_TX_PIN, 0);

    gpioWaveAddNew();

    gpioWaveAddGeneric(numPulses, pulse);

    wave_id = gpioWaveCreate();

    if (wave_id >= 0)
    {
        gpioWaveTxSend(wave_id, PI_WAVE_MODE_ONE_SHOT);
        sleep(1);
        gpioWaveTxStop();

        gpioWrite(GPIO_TX_PIN, 0);
    }
    else
    {
        QMessageBox::information(this, "Error", "Wave create failed for some reason");
    }
    return;

}


#define WITHIN_TOLERANCE_DC_HIGH(fall, rise, period)  \
    ((fall < (rise + .75 * period) + 20) && (fall > (rise + .75 * period) - 20))
#define WITHIN_TOLERANCE_DC_LOW(fall, rise, period)  \
    ((fall < (rise + .25 * period) + 20) && (fall > (rise + .25 * period) - 20))
#define WITHIN_TOLERANCE_DC_IDLE(fall, rise, period)  \
    ((fall < (rise + .50 * period) + 20) && (fall > (rise + .50 * period) - 20))
void MainWindow::buildChart(QVector<unsigned> riseTimes, QVector<unsigned> fallTimes, unsigned periodInUS, QVector<unsigned> bits)
{
    QT_CHARTS_USE_NAMESPACE

    QLineSeries *series = new QLineSeries();
    unsigned offset = riseTimes[0];

    for (int i = 0; i < fallTimes.length(); i++)
    {
        *series << QPointF(riseTimes[i]-offset, 0);
        *series << QPointF(riseTimes[i]-offset, 5);
        *series << QPointF(fallTimes[i]-offset, 5);
        *series << QPointF(fallTimes[i]-offset, 0);
        *series << QPointF(periodInUS+periodInUS*i, 0);

    }

    QChart *chart = new QChart();
    chart->addSeries(series);

    // Customize chart title
    QFont font;
    font.setPixelSize(18);
    chart->setTitleFont(font);
    chart->setTitleBrush(QBrush(Qt::white));
    chart->setTitle("Read Waveform");

    // Customize chart background
    QLinearGradient backgroundGradient;
    backgroundGradient.setStart(QPointF(0, 0));
    backgroundGradient.setFinalStop(QPointF(0, 1));
    backgroundGradient.setColorAt(0.0, QRgb(0xd2d0d1));
    backgroundGradient.setColorAt(1.0, QRgb(0x4c4547));
    backgroundGradient.setCoordinateMode(QGradient::ObjectBoundingMode);
    chart->setBackgroundBrush(backgroundGradient);

    QCategoryAxis *axisX = new QCategoryAxis();
    QCategoryAxis *axisY = new QCategoryAxis();
    QValueAxis *axisXVal = new QValueAxis();

    // Customize axis label font
    QFont labelsFont;
    labelsFont.setPixelSize(12);
    axisX->setLabelsFont(labelsFont);
    axisXVal->setLabelsFont(labelsFont);
    axisY->setLabelsFont(labelsFont);

    // Customize axis colors
    QPen axisPen(QRgb(0xd18952));
    axisPen.setWidth(2);
    axisX->setLinePen(axisPen);
    axisXVal->setLinePen(axisPen);
    axisY->setLinePen(axisPen);

    // Customize axis label colors
    QBrush axisBrush(Qt::white);
    axisX->setLabelsBrush(axisBrush);
    axisXVal->setLabelsBrush(axisBrush);
    axisY->setLabelsBrush(axisBrush);

    // Customize grid lines and shades
    axisX->setGridLineVisible(false);
    axisY->setGridLineVisible(false);
    axisX->setRange(0, riseTimes.last()+periodInUS);
    axisY->setShadesPen(Qt::NoPen);
    axisY->setShadesBrush(QBrush(QColor(0x99, 0xcc, 0xcc, 0x55)));
    axisY->setShadesVisible(false);
    //axisXVal->applyNiceNumbers();
    axisXVal->setTickCount(fallTimes.length()+1);
    axisXVal->setRange(0, riseTimes.last()+periodInUS);
    axisXVal->setTitleText("uSecs");
    axisXVal->setTitleVisible();
    axisXVal->setLabelsColor(QColor(255,255,255));
    axisXVal->setTitleBrush(QBrush(QColor(255,255,255)));

    // Bit vector only contains data for bits, not idle pulses.
    // To keep it syncrhonized with the whole waveform, we keep track
    // of the offset. Each idle pulse increments our offset

    // Type of pulse      IDLE ...........bits............... IDLE ........bi....
    // Waveform pulse idx: 0   1   2   3   4   5   6   7   8   9   10  11  12  13
    // Bit Vector Idx      NA  0   1   2   3   4   5   6   7   NA   8  9   10  11
    //                  Offset is 1 here!                     Offset is 2 here!

    // Anyway all that to explain this var:
    int numIdleBits = 0;
    for (int i = 0; i < fallTimes.length(); i++)
    {
        int relativeBit = i%9;
        // Note: the bit vector does not include idles. Thus, we skip them
        if (relativeBit == 0)
        {
            axisX->append("Idle Pulse " + QString::number(numIdleBits), riseTimes[i]+periodInUS);
            numIdleBits++;
        }
        else
        {
            QString bit = "Bit " + QString::number(i-numIdleBits) + "( " + QString::number(bits[i-numIdleBits]) + " )";
            axisX->append(bit, riseTimes[i]+periodInUS);
        }
    }

    axisY->append("Volts", 5);
    axisY->setRange(-5, 10);

    chart->setAxisX(axisX, series);
    chart->setAxisY(axisY, series);
    chart->addAxis(axisXVal, Qt::AlignBottom);

    QChartView *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    cw->setCentralWidget(chartView);

    cw->resize(700, 400);
    cw->show();
}

void MainWindow::on_Read_clicked()
{

    gpioPulse_t pulse[MAX_PULSES];

    QVector<unsigned> riseTimes;
    QVector<unsigned> fallTimes;
    QVector<unsigned> bits;

    waveform_t waveData;
    waveData.falling = &fallTimes;
    waveData.rising = &riseTimes;

    // Configure sample rate for the highest (1 uSec)
    // Documentation implies this will make cpu usage 25%, we don't care
    gpioCfgClock(2, 1, 0);

    if (gpioInitialise() < 0)
    {
        QMessageBox::information(this, "Error", "Cant init GPIO. Are you root?");
        return;
    }

    // Check the address
    bool ok;
    unsigned char address = ui->read_address->text().toUInt(&ok, 16);
    if (!ok || address > 0x7F)
    {
        QMessageBox::information(this, "Error", "Address is not a valid 7-bit hex value");
        return;
    }
    // Build the pulses for the read command
    unsigned numPulses = BuildPulses(READ, pulse, GPIO_TX_PIN, address, 0, 0);

    // Set the GPIO to output
    gpioSetMode(GPIO_TX_PIN, PI_OUTPUT);

    int wave_id;

    gpioWrite(GPIO_TX_PIN, 0);

    gpioWaveAddNew();

    gpioWaveAddGeneric(numPulses, pulse);

    wave_id = gpioWaveCreate();

    if (wave_id < 0)
    {
        QMessageBox::information(this, "Error", "Wave create failed for some reason");
        return;
    }

    // Init pin to input
    gpioSetMode(GPIO_RX_PIN, PI_INPUT);

    // Register the callback function
    gpioSetAlertFuncEx(GPIO_RX_PIN, _cb, &waveData);

    // Shoot out the read command
    gpioWaveTxSend(wave_id, PI_WAVE_MODE_ONE_SHOT);

    // Ensure it has gone out
    sleep(1);

    // Not necessary given the one shot but why not
    gpioWaveTxStop();

    // Un-register the callback
    gpioSetAlertFunc(GPIO_RX_PIN, 0);

    // Clean up
    gpioWrite(GPIO_TX_PIN, 0);

    QString resultString;

    // ---Analyze Waveform---

    // Sanity check
    if (fallTimes.length() != riseTimes.length())
    {
        QMessageBox::information(this, "Error",  "Number of falling edges doesnt equal number of rusing edges? This is confusing, goodbye.");
        return;
    }

    // Make absolute measurements relative
    unsigned offset = riseTimes[0];
    for (int i = 0; i < fallTimes.length(); i++)
    {
        riseTimes[i] -= offset;
        fallTimes[i] -= offset;
    }

    // Calculate the frequency (first pulse is idle bit)
    unsigned periodInUS = fallTimes[0];
    // Adjust for 50% duty cycle of idle bit
    periodInUS *= 2;
    // Calculate freq in KHZ
    double freq = 1/(double)periodInUS * 1000;

    // Build the 1's and 0's ignoring the idle bit
    for (int i = 1; i < fallTimes.length(); i++)
    {
        int relativeBitNum = (i-1)%8;
        if (WITHIN_TOLERANCE_DC_HIGH(fallTimes[i], riseTimes[i], periodInUS))
            bits.append(1);
        else if (WITHIN_TOLERANCE_DC_LOW(fallTimes[i], riseTimes[i], periodInUS))
            bits.append(0);
        // Expect Idle bit every byte
        else if (!(relativeBitNum % BITS_IN_BYTE == 0 &&
                   WITHIN_TOLERANCE_DC_IDLE(fallTimes[i], riseTimes[i], periodInUS)))
        {
            QMessageBox::information(this, "Error",  "Bad Read! Noise?");
            return;
        }

    }

    // Build the resulting bytes (8 always)
    unsigned result[8] = {0};
    for (int i = 0; i < bits.length(); i++)
    {
        result[i/8] |= bits[i] << i;
    }

    // Convert the data to a string for the user
    QString data;
    for (int i = 0; i < bits.length() / 8; i++)
    {
        if (i % 4 == 0) data += " 0x";
        data += QString::number(result[i], 16);
    }

    resultString = "Incoming freq seems to be about " +
            QString::number(freq, 'g', 6) +
            " khz." + " Data was" + data;

    // Build a chart for display (lets QT it up)
    buildChart(riseTimes, fallTimes, periodInUS, bits);

    // Display what we know
    QMessageBox::information(this, "Result Window",  resultString);


    return;
}


void MainWindow::on_checkBox_clicked()
{
    if (gpioInitialise() < 0)
    {
        QMessageBox::information(this, "Error", "Cant init GPIO. Are you root?");
        return;
    }


    // Set the GPIO to output
    gpioSetMode(GPIO_RD_EN_PIN, PI_OUTPUT);
    gpioSetMode(GPIO_WRT_EN_PIN, PI_OUTPUT);

    int logicLevel = (ui->checkBox->checkState() == Qt::Checked ? 1 : 0);

    gpioWrite(GPIO_RD_EN_PIN, logicLevel);
    gpioWrite(GPIO_WRT_EN_PIN, logicLevel);


    return;
}
