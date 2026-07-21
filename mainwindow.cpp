#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QComboBox>
#include <QCheckBox>
#include <QRegularExpression>
#include <QPair>
#include <cmath>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->inputHost->setText("192.168.53.");
    ui->inputUser->clear(); // lasat liber, completezi manual (root / user, in functie de placa)
    ui->inputBoardLabel->clear(); // gol = detectare automata

    connect(ui->btnFetchSpecs, &QPushButton::clicked, this, &MainWindow::onFetchSpecsClicked);
    connect(ui->btnCompare, &QPushButton::clicked, this, &MainWindow::onCompareClicked);

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimerTick);
    ui->spinRefreshInterval->setRange(5, 300);
    ui->spinRefreshInterval->setValue(10);
    ui->spinRefreshInterval->setSuffix(" s");
    connect(ui->chkAutoRefresh, &QCheckBox::toggled, this, &MainWindow::onAutoRefreshToggled);

    // Populam combo-urile de RAM (daca nu ai adaugat deja itemii direct din Designer)
    if (ui->comboPi4Ram->count() == 0) {
        ui->comboPi4Ram->addItems({"1GB", "2GB", "4GB", "8GB"});
        ui->comboPi4Ram->setCurrentText("4GB");
    }
    if (ui->comboPi5Ram->count() == 0) {
        ui->comboPi5Ram->addItems({"2GB", "4GB", "8GB", "16GB"});
        ui->comboPi5Ram->setCurrentText("4GB");
    }

    connect(ui->comboPi4Ram, &QComboBox::currentTextChanged, this, &MainWindow::onPi4RamChanged);
    connect(ui->comboPi5Ram, &QComboBox::currentTextChanged, this, &MainWindow::onPi5RamChanged);

    setupStaticPiSpecs();

    ui->labelActiveBoardSummary->setText("Nicio placă citită încă. Mergi în tab-ul 1 și apasă \"Citește specificații\".");
    ui->textComparisonResult->setPlainText("Comparația va apărea aici după ce ai date de la o placă activă.");
}

double MainWindow::parseRamComboValue(const QString &text) const
{
    QString cleaned = text;
    cleaned.remove("GB");
    bool ok = false;
    double value = cleaned.toDouble(&ok);
    return ok ? value : 4.0;
}

void MainWindow::setupStaticPiSpecs()
{
    m_pi4RamGB = parseRamComboValue(ui->comboPi4Ram->currentText());
    m_pi5RamGB = parseRamComboValue(ui->comboPi5Ram->currentText());
    updatePi4Label();
    updatePi5Label();
}

void MainWindow::updatePi4Label()
{
    ui->labelPi4Specs->setText(
        QString("Raspberry Pi 4 Model B (%1)\n"
                "SoC: Broadcom BCM2711\n"
                "CPU: Quad-core Cortex-A72 @ 1.5 GHz\n"
                "RAM: %1 LPDDR4-3200\n"
                "GPU: VideoCore VI\n"
                "USB: 2× USB 3.0, 2× USB 2.0\n"
                "Rețea: Gigabit Ethernet, WiFi ac, BT 5.0\n"
                "Video: 2× micro-HDMI (4Kp60 dual)\n"
                "Cameră/Display: 1× CSI, 1× DSI\n"
                "Stocare: microSD\n"
                "PCIe: intern (neexpus)\n"
                "Alimentare: USB-C 5V/3A").arg(ui->comboPi4Ram->currentText())
        );
}

void MainWindow::updatePi5Label()
{
    ui->labelPi5Specs->setText(
        QString("Raspberry Pi 5 (%1)\n"
                "SoC: Broadcom BCM2712\n"
                "CPU: Quad-core Cortex-A76 @ 2.4 GHz\n"
                "RAM: %1 LPDDR4X-4267\n"
                "GPU: VideoCore VII\n"
                "USB: 2× USB 3.0, 2× USB 2.0\n"
                "Rețea: Gigabit Ethernet (PoE), WiFi ac, BT 5.0 BLE\n"
                "Video: 2× micro-HDMI (4Kp60 dual)\n"
                "Cameră/Display: 2× MIPI 4-lane\n"
                "Stocare: microSD (SDR104) + PCIe NVMe\n"
                "PCIe: 2.0 x1 expus (M.2 prin HAT)\n"
                "Alimentare: USB-C 5V/5A PD\n"
                "Extra: buton pornire, RTC cu baterie").arg(ui->comboPi5Ram->currentText())
        );
}

void MainWindow::onPi4RamChanged(const QString &text)
{
    m_pi4RamGB = parseRamComboValue(text);
    updatePi4Label();
}

void MainWindow::onPi5RamChanged(const QString &text)
{
    m_pi5RamGB = parseRamComboValue(text);
    updatePi5Label();
}

void MainWindow::onAutoRefreshToggled(bool checked)
{
    if (checked) {
        m_refreshTimer->start(ui->spinRefreshInterval->value() * 1000);
        ui->spinRefreshInterval->setEnabled(false);
        onRefreshTimerTick(); // prima citire imediat, nu abia dupa primul interval
    } else {
        m_refreshTimer->stop();
        ui->spinRefreshInterval->setEnabled(true);
    }
}

void MainWindow::onRefreshTimerTick()
{
    if (m_fetchInProgress) {
        return; // sarim peste tick daca cererea anterioara inca nu s-a terminat
    }
    onFetchSpecsClicked();
}

void MainWindow::onFetchSpecsClicked()
{
    QString host = ui->inputHost->text().trimmed();
    QString boardLabelOverride = ui->inputBoardLabel->text().trimmed();
    QString user = ui->inputUser->text().trimmed();
    if (user.isEmpty()) {
        user = "root"; // fallback implicit, ex. pentru FRDM
    }

    if (host.isEmpty()) {
        ui->textActiveBoardStatus->setPlainText("Completează IP-ul plăcii înainte de a citi specificațiile.");
        return;
    }

    // boardLabelOverride poate fi gol -- se detecteaza automat de pe placa (hostname + model)
    fetchBoardSpecs(host, user, ui->textActiveBoardStatus, boardLabelOverride);
}

QString MainWindow::friendlyCpuName(const QString &cpuPartRaw)
{
    if (cpuPartRaw.contains("0xd03")) return cpuPartRaw + " — ARM Cortex-A53";
    if (cpuPartRaw.contains("0xd08")) return cpuPartRaw + " — ARM Cortex-A72";
    if (cpuPartRaw.contains("0xd0b")) return cpuPartRaw + " — ARM Cortex-A76";
    return cpuPartRaw;
}

void MainWindow::fetchBoardSpecs(const QString &host, const QString &user, QPlainTextEdit *targetWidget, const QString &boardLabelOverride)
{
    m_fetchInProgress = true;

    if (targetWidget) {
        QString pending = boardLabelOverride.isEmpty()
        ? "Se conectează la " + host + " (detectare automată a plăcii)..."
        : "Se conectează la " + boardLabelOverride + " (" + host + ")...";
        targetWidget->setPlainText(pending);
    }

    QProcess *process = new QProcess(this);
    QString sshTarget = QString("%1@%2").arg(user, host);

    QString remoteCmd =
        "echo '###HOSTNAME###'; hostname; "
        "echo '###MODEL###'; cat /proc/device-tree/model 2>/dev/null | tr -d '\\0'; "
        "echo '###CPU_CORES###'; grep -c ^processor /proc/cpuinfo; "
        "echo '###CPU_PART###'; grep 'CPU part' /proc/cpuinfo | head -1 | awk -F': ' '{print $2}'; "
        "echo '###CPU_FREQ###'; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null; "
        "echo '###MEM###'; free -h | awk '/Mem:/ {print $2\",\"$3\",\"$4}'; "
        "echo '###MEM_TOTAL_KB###'; grep MemTotal /proc/meminfo | awk '{print $2}'; "
        "echo '###DISK###'; df -h / | awk 'NR==2 {print $2\",\"$3\",\"$4\",\"$5}'; "
        "echo '###PARTITIONS###'; df -h --output=source,fstype,size,used,avail,pcent,target 2>/dev/null | tail -n +2 | grep -Ev '^(tmpfs|devtmpfs|proc|sysfs|cgroup|cgroup2|devpts|mqueue|hugetlbfs|squashfs|none)[[:space:]]'; "
        "echo '###FREESPACE###'; lsblk -nlbo NAME,SIZE,TYPE,PKNAME,FSTYPE 2>/dev/null; "
        "echo '###BLOCKLAYOUT###'; for d in /sys/class/block/*/; do n=$(basename \"$d\"); "
        "if [ -f \"${d}start\" ] && [ -f \"${d}size\" ]; then echo \"$n $(cat ${d}start) $(cat ${d}size)\"; fi; done; "
        "echo '###KERNEL###'; uname -r; "
        "echo '###OS###'; cat /etc/os-release | grep PRETTY_NAME | cut -d'\"' -f2; "
        "echo '###UPTIME###'; uptime -p; "
        "echo '###ROS2###'; ls /opt/ros/ 2>/dev/null; "
        "echo '###ROS_CHECK###'; RD=$(ls /opt/ros/ 2>/dev/null | head -1); "
        "if [ -n \"$RD\" ] && [ -f /opt/ros/$RD/setup.bash ]; then "
        ". /opt/ros/$RD/setup.bash 2>/dev/null; "
        "echo \"DISTRO:$RD\"; "
        "echo '---NODES-START---'; timeout 4 ros2 node list 2>/dev/null; echo '---NODES-END---'; "
        "echo '---TOPICS-START---'; timeout 4 ros2 topic list 2>/dev/null; echo '---TOPICS-END---'; "
        "else echo 'DISTRO:none'; fi";

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, targetWidget, boardLabelOverride, host](int exitCode, QProcess::ExitStatus) {

                if (exitCode != 0) {
                    if (targetWidget) {
                        QString labelForError = boardLabelOverride.isEmpty() ? host : boardLabelOverride;
                        targetWidget->setPlainText("Eroare conectare SSH la " + labelForError + " (" + host + ")\n"
                                                   + QString::fromUtf8(process->readAllStandardError()));
                    }
                    process->deleteLater();
                    m_fetchInProgress = false;
                    return;
                }

                QString output = QString::fromUtf8(process->readAllStandardOutput());
                QMap<QString, QString> specs;

                QStringList sections = output.split("###", Qt::SkipEmptyParts);
                for (int i = 0; i + 1 < sections.size(); i += 2) {
                    QString key = sections[i].trimmed();
                    QString value = sections[i + 1].trimmed();
                    specs[key] = value;
                }

                // Determinam eticheta placii: override manual > (hostname + model detectate) > doar host
                QString detectedHostname = specs.value("HOSTNAME", "").simplified();
                QString detectedModel = specs.value("MODEL", "").simplified();

                QString boardLabel;
                if (!boardLabelOverride.isEmpty()) {
                    boardLabel = boardLabelOverride;
                } else if (!detectedModel.isEmpty() && !detectedHostname.isEmpty()) {
                    boardLabel = detectedModel + " (" + detectedHostname + ")";
                } else if (!detectedModel.isEmpty()) {
                    boardLabel = detectedModel;
                } else if (!detectedHostname.isEmpty()) {
                    boardLabel = detectedHostname;
                } else {
                    boardLabel = host; // fallback ultim, daca nu s-a putut detecta nimic
                }

                QString cores = specs.value("CPU_CORES", "N/A").simplified();
                QString cpuPartRaw = specs.value("CPU_PART", "N/A").simplified();
                QString cpuFriendly = friendlyCpuName(cpuPartRaw);

                QStringList memParts = specs.value("MEM").split(",");
                QString memDisplay = memParts.size() == 3
                                         ? QString("%1 total, %2 folosit, %3 liber").arg(memParts[0], memParts[1], memParts[2])
                                         : specs.value("MEM", "N/A");

                QStringList diskParts = specs.value("DISK").split(",");
                QString diskDisplay = diskParts.size() == 4
                                          ? QString("%1 total, %2 folosit, %3 liber (%4)").arg(diskParts[0], diskParts[1], diskParts[2], diskParts[3])
                                          : specs.value("DISK", "N/A");

                // Parsam lista de partitii (fiecare linie: sursa, tip_fs, size, used, avail, pcent, mount_point)
                QString partitionsRaw = specs.value("PARTITIONS", "").trimmed();
                QStringList partitionLines = partitionsRaw.split("\n", Qt::SkipEmptyParts);
                QString partitionsDisplay;
                QStringList partitionRowsForFile;
                for (const QString &line : partitionLines) {
                    QStringList cols = line.simplified().split(" ");
                    if (cols.size() >= 7) {
                        QString source = cols[0];
                        QString fstype = cols[1];
                        QString size = cols[2];
                        QString used = cols[3];
                        QString avail = cols[4];
                        QString pcent = cols[5];
                        QString mount = cols[6];
                        partitionsDisplay += QString("  %1  (%2)  →  %3 total, %4 folosit, %5 liber (%6)  [%7]\n")
                                                 .arg(mount, fstype, size, used, avail, pcent, source);
                        partitionRowsForFile << QString("%1|%2|%3|%4|%5|%6|%7")
                                                    .arg(source, fstype, size, used, avail, pcent, mount);
                    }
                }
                if (partitionsDisplay.isEmpty()) {
                    partitionsDisplay = "  (nicio partiție reală detectată)\n";
                }

                // Calculam spatiul nealocat din lsblk: pentru fiecare disc fizic, size total - suma partitiilor lui.
                // Nu necesita sudo/parted -- doar lsblk, care e disponibil fara privilegii speciale.
                QString lsblkRaw = specs.value("FREESPACE", "").trimmed();
                QStringList lsblkLines = lsblkRaw.split("\n", Qt::SkipEmptyParts);

                QMap<QString, qint64> diskSizeBytes;
                QMap<QString, qint64> diskUsedByPartitions;
                QStringList diskOrder;
                QMap<QString, QString> partitionParentDisk;
                QMap<QString, QString> partitionFstype;
                QMap<QString, qint64> partitionSizeBytes;

                for (const QString &line : lsblkLines) {
                    QStringList cols = line.simplified().split(" ");
                    if (cols.size() < 3) continue;
                    QString name = cols[0];
                    qint64 sizeBytes = cols[1].toLongLong();
                    QString type = cols[2];
                    QString pkname = cols.size() >= 4 ? cols[3] : "";
                    QString fstype = cols.size() >= 5 ? cols[4] : "";

                    if (type == "disk") {
                        if (name.contains("boot")) continue; // sarim mmcblk*boot0/boot1
                        diskSizeBytes[name] = sizeBytes;
                        if (!diskOrder.contains(name)) diskOrder << name;
                    } else if (type == "part" && !pkname.isEmpty()) {
                        diskUsedByPartitions[pkname] += sizeBytes;
                        partitionParentDisk[name] = pkname;
                        partitionFstype[name] = fstype.isEmpty() ? "necunoscut" : fstype;
                        partitionSizeBytes[name] = sizeBytes;
                    }
                }

                // Layout fizic exact (start/size in sectoare de 512 octeti), citit din /sys/class/block, fara sudo
                QString blockLayoutRaw = specs.value("BLOCKLAYOUT", "").trimmed();
                QStringList blockLayoutLines = blockLayoutRaw.split("\n", Qt::SkipEmptyParts);
                QMap<QString, qint64> partitionStartBytes;
                const qint64 SECTOR = 512;
                for (const QString &line : blockLayoutLines) {
                    QStringList cols = line.simplified().split(" ");
                    if (cols.size() != 3) continue;
                    QString name = cols[0];
                    qint64 startSectors = cols[1].toLongLong();
                    if (!partitionParentDisk.contains(name)) continue; // doar partitii reale, nu discuri intregi
                    partitionStartBytes[name] = startSectors * SECTOR;
                }

                QString freeSpaceDisplay;
                double totalFreeMB = 0.0;
                for (const QString &disk : diskOrder) {
                    qint64 total = diskSizeBytes.value(disk, 0);
                    qint64 used = diskUsedByPartitions.value(disk, 0);
                    qint64 unallocated = total - used;
                    if (unallocated < 0) unallocated = 0;

                    double totalGB = total / 1e9;
                    double usedGB = used / 1e9;
                    double unallocGB = unallocated / 1e9;
                    totalFreeMB += unallocated / 1e6;

                    freeSpaceDisplay += QString("  /dev/%1 → %2 GB fizic total, %3 GB în partiții, %4 GB nealocat\n")
                                            .arg(disk)
                                            .arg(totalGB, 0, 'f', 2)
                                            .arg(usedGB, 0, 'f', 2)
                                            .arg(unallocGB, 0, 'f', 2);
                }
                if (freeSpaceDisplay.isEmpty()) {
                    freeSpaceDisplay = "  (nu s-a putut citi structura discului via lsblk)\n";
                }

                // Tabel ordonat, in ordinea fizica de pe disc (ca la "parted print free"), dar fara sudo
                QString layoutDisplay;
                for (const QString &disk : diskOrder) {
                    qint64 diskTotal = diskSizeBytes.value(disk, 0);

                    // Adunam partitiile acestui disc, cu pozitia lor de start
                    QList<QPair<qint64, QString>> parts; // (start, nume)
                    for (auto it = partitionParentDisk.constBegin(); it != partitionParentDisk.constEnd(); ++it) {
                        if (it.value() == disk && partitionStartBytes.contains(it.key())) {
                            parts.append({partitionStartBytes[it.key()], it.key()});
                        }
                    }
                    std::sort(parts.begin(), parts.end(), [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) {
                        return a.first < b.first;
                    });

                    layoutDisplay += QString("\n  Structură fizică /dev/%1 (%2 GB):\n").arg(disk).arg(diskTotal / 1e9, 0, 'f', 2);

                    auto fmtGB = [](qint64 bytes) { return QString::number(bytes / 1e9, 'f', 3); };
                    qint64 cursor = 0;
                    const qint64 GAP_THRESHOLD = 1000000; // ignoram goluri sub 1MB (rotunjiri)

                    for (const auto &p : parts) {
                        qint64 start = p.first;
                        QString name = p.second;
                        qint64 size = partitionSizeBytes.value(name, 0);

                        if (start - cursor > GAP_THRESHOLD) {
                            layoutDisplay += QString("    [liber]  %1 → %2 GB  (%3 GB)\n")
                                                 .arg(fmtGB(cursor), fmtGB(start))
                                                 .arg(fmtGB(start - cursor));
                        }

                        layoutDisplay += QString("    %1  (%2)  %3 → %4 GB  (%5 GB)\n")
                                             .arg(name, partitionFstype.value(name, "?"))
                                             .arg(fmtGB(start), fmtGB(start + size))
                                             .arg(fmtGB(size));

                        cursor = start + size;
                    }

                    if (diskTotal - cursor > GAP_THRESHOLD) {
                        layoutDisplay += QString("    [liber]  %1 → %2 GB  (%3 GB)\n")
                                             .arg(fmtGB(cursor), fmtGB(diskTotal))
                                             .arg(fmtGB(diskTotal - cursor));
                    }
                }

                bool freqOk = false;
                double freqKHz = specs.value("CPU_FREQ").simplified().toDouble(&freqOk);
                double freqGHz = 0.0;
                QString freqDisplay;
                if (freqOk && freqKHz > 0) {
                    freqGHz = freqKHz / 1000000.0;
                    freqDisplay = QString::number(freqGHz, 'f', 2) + " GHz";
                } else {
                    // Fallback: cpufreq indisponibil pe acest kernel -- folosim frecventa nominala cunoscuta
                    // pentru CPU part-ul detectat (etichetata explicit ca "nominal", nu "curent").
                    if (cpuPartRaw.contains("0xd03")) { freqGHz = 1.8; freqDisplay = "~1.80 GHz (nominal, cpufreq indisponibil)"; }
                    else { freqGHz = 0.0; freqDisplay = "N/A"; }
                }

                bool memOk = false;
                double memTotalKB = specs.value("MEM_TOTAL_KB").simplified().toDouble(&memOk);
                double ramTotalGB = memOk ? (memTotalKB / 1024.0 / 1024.0) : 0.0;

                QString display;
                display += "Placă: " + boardLabel + " (" + host + ")\n";
                display += "CPU: " + cpuFriendly + " (" + cores + " nuclee, " + freqDisplay + ")\n";
                display += "RAM: " + memDisplay + "\n";
                display += "Disc (/): " + diskDisplay + "\n";
                display += "\nPartiții montate:\n" + partitionsDisplay;
                display += "\nSpațiu fizic pe disc (din lsblk, fără sudo):\n" + freeSpaceDisplay;
                if (totalFreeMB > 0) {
                    double totalFreeGB = totalFreeMB / 1000.0;
                    display += QString("  → Total nealocat (toate discurile): ~%1 GB\n").arg(totalFreeGB, 0, 'f', 2);
                }
                display += layoutDisplay;
                display += "Kernel: " + specs.value("KERNEL", "N/A").simplified() + "\n";
                display += "OS: " + specs.value("OS", "N/A").simplified() + "\n";
                display += "Uptime: " + specs.value("UPTIME", "N/A").simplified() + "\n";
                display += "ROS2: " + specs.value("ROS2", "N/A").simplified() + "\n";

                // Verificare live ROS2 (independenta de versiune -- humble, jazzy, etc.)
                QString rosCheckRaw = specs.value("ROS_CHECK", "");
                QString rosDistroDetected;
                for (const QString &line : rosCheckRaw.split("\n", Qt::SkipEmptyParts)) {
                    QString l = line.trimmed();
                    if (l.startsWith("DISTRO:")) { rosDistroDetected = l.mid(7).trimmed(); break; }
                }

                auto extractBlock = [&rosCheckRaw](const QString &startMarker, const QString &endMarker) -> QStringList {
                    int startIdx = rosCheckRaw.indexOf(startMarker);
                    int endIdx = rosCheckRaw.indexOf(endMarker);
                    if (startIdx < 0 || endIdx < 0 || endIdx <= startIdx) return {};
                    QString block = rosCheckRaw.mid(startIdx + startMarker.length(), endIdx - startIdx - startMarker.length());
                    QStringList lines;
                    for (const QString &l : block.split("\n", Qt::SkipEmptyParts)) {
                        QString trimmed = l.trimmed();
                        if (!trimmed.isEmpty()) lines << trimmed;
                    }
                    return lines;
                };

                QStringList rosNodes = extractBlock("---NODES-START---", "---NODES-END---");
                QStringList rosTopics = extractBlock("---TOPICS-START---", "---TOPICS-END---");

                if (rosDistroDetected.isEmpty() || rosDistroDetected == "none") {
                    display += "Verificare ROS2 live: nu s-a găsit nicio distribuție instalată\n";
                } else {
                    display += QString("\nVerificare ROS2 live (%1):\n").arg(rosDistroDetected);
                    display += QString("  Noduri active (%1):\n").arg(rosNodes.size());
                    if (rosNodes.isEmpty()) {
                        display += "    (niciun nod activ)\n";
                    } else {
                        for (const QString &n : rosNodes) display += "    " + n + "\n";
                    }
                    display += QString("  Topicuri active (%1):\n").arg(rosTopics.size());
                    if (rosTopics.isEmpty()) {
                        display += "    (niciun topic activ)\n";
                    } else {
                        for (const QString &t : rosTopics) display += "    " + t + "\n";
                    }
                }
                display += "Actualizat: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

                if (targetWidget) {
                    targetWidget->setPlainText(display);
                }

                m_activeSpecs.valid = true;
                m_activeSpecs.label = boardLabel;
                m_activeSpecs.cores = cores.toInt();
                m_activeSpecs.cpuPart = cpuPartRaw;
                m_activeSpecs.cpuFriendly = cpuFriendly;
                m_activeSpecs.ramTotalGB = ramTotalGB;
                m_activeSpecs.cpuFreqGHz = freqGHz;

                QString activeSummary;
                activeSummary += boardLabel + "\n";
                activeSummary += cpuFriendly + "\n";
                activeSummary += cores + " nuclee, " + freqDisplay + "\n";
                activeSummary += QString::number(ramTotalGB, 'f', 1) + " GB RAM (fizic)";
                ui->labelActiveBoardSummary->setText(activeSummary);

                QMap<QString, QString> flatSpecs;
                flatSpecs["CPU"] = cpuFriendly + " (" + cores + " nuclee, " + freqDisplay + ")";
                flatSpecs["MEM"] = memDisplay;
                flatSpecs["DISK"] = diskDisplay;
                flatSpecs["KERNEL"] = specs.value("KERNEL").simplified();
                flatSpecs["OS"] = specs.value("OS").simplified();
                flatSpecs["UPTIME"] = specs.value("UPTIME").simplified();
                flatSpecs["ROS2"] = specs.value("ROS2").simplified();
                saveSpecsToFile(boardLabel, flatSpecs);
                savePartitionsToFile(boardLabel, partitionRowsForFile);

                process->deleteLater();
                m_fetchInProgress = false;
            });

    process->start("ssh", QStringList()
                              << "-o" << "ConnectTimeout=5"
                              << sshTarget << remoteCmd);
}

// Factor orientativ de performanta per nucleu, relativ la Cortex-A53 (=1.0).
// ATENTIE: nu e un benchmark real, e o aproximare foarte generala bazata pe
// generatia de microarhitectura, doar pentru a avea un punct de plecare vizual.
double MainWindow::relativeIpcFactor(const QString &cpuPartRaw) const
{
    if (cpuPartRaw.contains("0xd03")) return 1.0;  // Cortex-A53
    if (cpuPartRaw.contains("0xd08")) return 1.8;  // Cortex-A72
    if (cpuPartRaw.contains("0xd0b")) return 2.6;  // Cortex-A76
    return 1.0;
}

void MainWindow::onCompareClicked()
{
    if (!m_activeSpecs.valid) {
        ui->textComparisonResult->setPlainText(
            "Nu există încă date de la o placă activă.\n"
            "Mergi în tab-ul \"Diagnosticare live\" și apasă \"Citește specificații\" mai întâi."
            );
        return;
    }

    const QString pi4CpuPart = "0xd08"; // Cortex-A72
    const double pi4FreqGHz = 1.5;
    const int pi4Cores = 4;

    const QString pi5CpuPart = "0xd0b"; // Cortex-A76
    const double pi5FreqGHz = 2.4;
    const int pi5Cores = 4;

    auto perfIndex = [this](int cores, double freqGHz, const QString &cpuPart) {
        return cores * freqGHz * relativeIpcFactor(cpuPart);
    };

    double activePerf = perfIndex(m_activeSpecs.cores, m_activeSpecs.cpuFreqGHz, m_activeSpecs.cpuPart);
    double pi4Perf = perfIndex(pi4Cores, pi4FreqGHz, pi4CpuPart);
    double pi5Perf = perfIndex(pi5Cores, pi5FreqGHz, pi5CpuPart);

    // Index de "capacitate" = index CPU ajustat de RAM (radical, ca sa nu explodeze liniar).
    // Baseline 4GB = factor 1.0. Mai mult RAM => bonus; mai putin RAM => penalizare.
    auto ramFactor = [](double ramGB) {
        return std::sqrt(ramGB / 4.0);
    };

    double activeCapacity = activePerf * ramFactor(m_activeSpecs.ramTotalGB);
    double pi4Capacity = pi4Perf * ramFactor(m_pi4RamGB);
    double pi5Capacity = pi5Perf * ramFactor(m_pi5RamGB);

    auto describeRatio = [](double ratio) {
        if (ratio > 1.03) return QString("mai rapid");
        if (ratio < 0.97) return QString("mai lent");
        return QString("comparabil");
    };

    QString result;
    result += QString("=== Comparație: %1 vs Raspberry Pi 4 (%2) / Pi 5 (%3) ===\n\n")
                  .arg(m_activeSpecs.label)
                  .arg(ui->comboPi4Ram->currentText())
                  .arg(ui->comboPi5Ram->currentText());

    result += QString("Nuclee:  %1 activ  |  %2 Pi4  |  %3 Pi5\n")
                  .arg(m_activeSpecs.cores).arg(pi4Cores).arg(pi5Cores);

    QString activeFreqStr = m_activeSpecs.cpuFreqGHz > 0
                                ? QString::number(m_activeSpecs.cpuFreqGHz, 'f', 2) + " GHz"
                                : "N/A";
    result += QString("Frecvență:  %1 activ  |  %2 GHz Pi4  |  %3 GHz Pi5\n")
                  .arg(activeFreqStr).arg(pi4FreqGHz, 0, 'f', 2).arg(pi5FreqGHz, 0, 'f', 2);

    result += QString("RAM:  %1 GB activ  |  %2 Pi4  |  %3 Pi5\n")
                  .arg(m_activeSpecs.ramTotalGB, 0, 'f', 1)
                  .arg(ui->comboPi4Ram->currentText())
                  .arg(ui->comboPi5Ram->currentText());

    result += "\n--- Index CPU (viteza brută, neafectat de RAM) ---\n";
    result += QString("%1: %2\n").arg(m_activeSpecs.label).arg(activePerf, 0, 'f', 1);
    result += QString("Raspberry Pi 4: %1\n").arg(pi4Perf, 0, 'f', 1);
    result += QString("Raspberry Pi 5: %1\n").arg(pi5Perf, 0, 'f', 1);

    if (activePerf > 0) {
        double r4 = activePerf / pi4Perf;
        double r5 = activePerf / pi5Perf;
        result += QString("\n%1 este de ~%2x față de Pi4 (%3) și ~%4x față de Pi5 (%5)\n")
                      .arg(m_activeSpecs.label)
                      .arg(r4, 0, 'f', 2).arg(describeRatio(r4))
                      .arg(r5, 0, 'f', 2).arg(describeRatio(r5));
    }

    result += "\n--- Index capacitate (CPU + RAM combinate) ---\n";
    result += QString("%1: %2\n").arg(m_activeSpecs.label).arg(activeCapacity, 0, 'f', 1);
    result += QString("Raspberry Pi 4 (%1): %2\n").arg(ui->comboPi4Ram->currentText()).arg(pi4Capacity, 0, 'f', 1);
    result += QString("Raspberry Pi 5 (%1): %2\n").arg(ui->comboPi5Ram->currentText()).arg(pi5Capacity, 0, 'f', 1);

    if (activeCapacity > 0) {
        double rc4 = activeCapacity / pi4Capacity;
        double rc5 = activeCapacity / pi5Capacity;
        result += QString("\n%1 are o capacitate de ~%2x față de Pi4 (%3) și ~%4x față de Pi5 (%5)\n")
                      .arg(m_activeSpecs.label)
                      .arg(rc4, 0, 'f', 2).arg(describeRatio(rc4))
                      .arg(rc5, 0, 'f', 2).arg(describeRatio(rc5));
    }

    ui->textComparisonResult->setPlainText(result);
}

void MainWindow::saveSpecsToFile(const QString &boardLabel, const QMap<QString, QString> &specs)
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString filePath = dataDir + "/board_specs_log.csv";

    bool fileExists = QFile::exists(filePath);
    QFile file(filePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        if (!fileExists) {
            out << "timestamp,board,cpu,mem,disk,kernel,os,uptime,ros2\n";
        }
        out << QDateTime::currentDateTime().toString(Qt::ISODate) << ","
            << boardLabel.trimmed().replace(",", ";") << ","
            << specs.value("CPU").replace(",", ";") << ","
            << specs.value("MEM").replace(",", ";") << ","
            << specs.value("DISK").replace(",", ";") << ","
            << specs.value("KERNEL") << ","
            << specs.value("OS").replace(",", ";") << ","
            << specs.value("UPTIME") << ","
            << specs.value("ROS2") << "\n";
        file.close();
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::savePartitionsToFile(const QString &boardLabel, const QStringList &partitionRows)
{
    if (partitionRows.isEmpty()) return;

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString filePath = dataDir + "/partitions_log.csv";

    bool fileExists = QFile::exists(filePath);
    QFile file(filePath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        if (!fileExists) {
            out << "timestamp,board,source,fstype,size,used,avail,pcent,mount_point\n";
        }
        QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
        QString boardClean = boardLabel.trimmed().replace(",", ";");
        for (const QString &row : partitionRows) {
            QStringList cols = row.split("|");
            if (cols.size() != 7) continue;
            out << timestamp << "," << boardClean << ","
                << cols[0] << "," << cols[1] << "," << cols[2] << ","
                << cols[3] << "," << cols[4] << "," << cols[5] << "," << cols[6] << "\n";
        }
        file.close();
    }
}