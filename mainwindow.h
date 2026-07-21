#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QProcess>
#include <QPlainTextEdit>
#include <QMap>
#include <QTimer>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

struct ActiveBoardSpecs {
    bool valid = false;
    QString label;
    int cores = 0;
    QString cpuPart;      // ex. 0xd03
    QString cpuFriendly;  // ex. ARM Cortex-A53
    double ramTotalGB = 0.0;
    double cpuFreqGHz = 0.0; // 0 daca nu s-a putut citi
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onFetchSpecsClicked();
    void onCompareClicked();
    void onPi4RamChanged(const QString &text);
    void onPi5RamChanged(const QString &text);
    void onAutoRefreshToggled(bool checked);
    void onRefreshTimerTick();

private:
    Ui::MainWindow *ui;
    ActiveBoardSpecs m_activeSpecs;

    double m_pi4RamGB = 4.0;
    double m_pi5RamGB = 4.0;
    QTimer *m_refreshTimer = nullptr;
    bool m_fetchInProgress = false;

    void fetchBoardSpecs(const QString &host, const QString &user, QPlainTextEdit *targetWidget, const QString &boardLabelOverride);
    void saveSpecsToFile(const QString &boardLabel, const QMap<QString, QString> &specs);
    void savePartitionsToFile(const QString &boardLabel, const QStringList &partitionRows);
    QString friendlyCpuName(const QString &cpuPartRaw);
    void setupStaticPiSpecs();
    void updatePi4Label();
    void updatePi5Label();
    double relativeIpcFactor(const QString &cpuPartRaw) const;
    double parseRamComboValue(const QString &text) const;
};
#endif // MAINWINDOW_H