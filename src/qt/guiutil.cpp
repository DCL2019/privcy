#include "guiutil.h"
#include "bitcoinaddressvalidator.h"
#include "walletmodel.h"
#include "bitcoinunits.h"
#include "util.h"
#include "init.h"

#include <QString>
#include <QDateTime>
#include <QDoubleValidator>
#include <QFont>
#include <QLineEdit>
#include <QUrl>
#include <QUrlQuery>
#include <QTextDocument> // For Qt::escape
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QDesktopServices>
#include <QThread>
#include <QSettings>
#include <QDesktopWidget>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#ifdef WIN32
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501
#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501
#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "shlwapi.h"
#include "shlobj.h"
#include "shellapi.h"
#endif

static fs::detail::utf8_codecvt_facet utf8;

namespace GUIUtil
{

QString dateTimeStr(const QDateTime &date)
{
    return date.date().toString(Qt::SystemLocaleShortDate) + QString(" ") + date.toString("hh:mm");
}

QString dateTimeStr(qint64 nTime)
{
    return dateTimeStr(QDateTime::fromTime_t((qint32)nTime));
}

QString formatDurationStr(int secs)
{
    QStringList strList;
    int days = secs / 86400;
    int hours = (secs % 86400) / 3600;
    int mins = (secs % 3600) / 60;
    int seconds = secs % 60;

    if (days)
        strList.append(QString(QObject::tr("%1 d")).arg(days));
    if (hours)
        strList.append(QString(QObject::tr("%1 h")).arg(hours));
    if (mins)
        strList.append(QString(QObject::tr("%1 m")).arg(mins));
    if (seconds || (!days && !hours && !mins))
        strList.append(QString(QObject::tr("%1 s")).arg(seconds));

    return strList.join(" ");
}

QString formatServicesStr(quint64 mask)
{
    QStringList strList;

    // TODO: for now, all nodes are simply NETWORK nodes
    strList.append("NETWORK");

    if (strList.size())
        return strList.join(", ");
    else
        return QObject::tr("None");
}

QString formatPingTime(double dPingTime)
{
    return dPingTime == 0 ? QObject::tr("N/A") : QString(QObject::tr("%1 ms")).arg(QString::number((int)(dPingTime * 1000), 10));
}

QString getEntryData(QAbstractItemView *view, int column, int role)
{
    if (!view || !view->selectionModel())
        return QString();
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if (!selection.isEmpty())
    {
        // Return first item
        return (selection.at(0).data(role).toString());
    }
    return QString();
}

QString formatTimeOffset(int64_t nTimeOffset)
{
    return QString(QObject::tr("%1 s")).arg(QString::number((int)nTimeOffset, 10));
}

QFont bitcoinAddressFont()
{
    QFont font("Monospace");
#if QT_VERSION >= 0x040800
    font.setStyleHint(QFont::Monospace);
#else
    font.setStyleHint(QFont::TypeWriter);
#endif
    return font;
}

void saveWindowGeometry(const QString &strSetting, QWidget *parent)
{
    QSettings settings;
    settings.setValue(strSetting + "Pos", parent->pos());
    settings.setValue(strSetting + "Size", parent->size());
}

void restoreWindowGeometry(const QString &strSetting, const QSize &defaultSize, QWidget *parent)
{
    QSettings settings;
    QPoint pos = settings.value(strSetting + "Pos").toPoint();
    QSize size = settings.value(strSetting + "Size", defaultSize).toSize();

    parent->resize(size);
    parent->move(pos);

    if ((!pos.x() && !pos.y()) || (QApplication::desktop()->screenNumber(parent) == -1))
    {
        QRect screen = QApplication::desktop()->screenGeometry();
        QPoint defaultPos = screen.center() -
                            QPoint(defaultSize.width() / 2, defaultSize.height() / 2);
        parent->resize(defaultSize);
        parent->move(defaultPos);
    }
}

QString loadStyleSheet()
{
    QString styleSheet;
    QSettings settings;

    QString cssName = QString(":/css/default");
    if (GetBoolArg("-dark"))
       cssName = QString(":/css/dark");
    else
       cssName = QString(":/css/light");

    QFile qFile(cssName);
    if (qFile.open(QFile::ReadOnly))
    {
        styleSheet = QLatin1String(qFile.readAll());
    }

    return styleSheet;
}

void setupAddressWidget(QLineEdit *widget, QWidget *parent)
{
    widget->setMaxLength(BitcoinAddressValidator::MaxAddressLength);
    widget->setValidator(new BitcoinAddressValidator(parent));
    widget->setFont(bitcoinAddressFont());
}

void setupAmountWidget(QLineEdit *widget, QWidget *parent)
{
    QDoubleValidator *amountValidator = new QDoubleValidator(parent);
    amountValidator->setDecimals(8);
    amountValidator->setBottom(0.0);
    widget->setValidator(amountValidator);
    widget->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

bool parseBitcoinURI(const QUrl &uri, SendCoinsRecipient *out)
{
    // NovaCoin: check prefix
    if (uri.scheme() != QString("PRiVCY"))
        return false;

    SendCoinsRecipient rv;
    rv.address = uri.path();
    rv.amount = 0;
    QUrlQuery *qry = new QUrlQuery(uri);
    QList<QPair<QString, QString> > items = qry->queryItems();
    for (QList<QPair<QString, QString> >::iterator i = items.begin(); i != items.end(); i++)
    {
        bool fShouldReturnFalse = false;
        if (i->first.startsWith("req-"))
        {
            i->first.remove(0, 4);
            fShouldReturnFalse = true;
        }

        if (i->first == "label")
        {
            rv.label = i->second;
            fShouldReturnFalse = false;
        }
        else if (i->first == "amount")
        {
            if (!i->second.isEmpty())
            {
                if (!BitcoinUnits::parse(BitcoinUnits::BTC, i->second, &rv.amount))
                {
                    return false;
                }
            }
            fShouldReturnFalse = false;
        }

        if (fShouldReturnFalse)
            return false;
    }
    if (out)
    {
        *out = rv;
    }
    return true;
}

bool parseBitcoinURI(QString uri, SendCoinsRecipient *out)
{
    // Convert PRiVCY:// to PRiVCY:
    //
    //    Cannot handle this later, because bitcoin:// will cause Qt to see the part after // as host,
    //    which will lower-case it (and thus invalidate the address).
    if (uri.startsWith("PRiVCY://"))
    {
        uri.replace(0, 12, "PRiVCY:");
    }
    QUrl uriInstance(uri);
    return parseBitcoinURI(uriInstance, out);
}

QString HtmlEscape(const QString &str, bool fMultiLine)
{
    QString escaped = str.toHtmlEscaped();
    if (fMultiLine)
    {
        escaped = escaped.replace("\n", "<br>\n");
    }
    return escaped;
}

QString HtmlEscape(const std::string &str, bool fMultiLine)
{
    return HtmlEscape(QString::fromStdString(str), fMultiLine);
}

void copyEntryData(QAbstractItemView *view, int column, int role)
{
    if (!view || !view->selectionModel())
        return;
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if (!selection.isEmpty())
    {
        // Copy first item
        QApplication::clipboard()->setText(selection.at(0).data(role).toString());
    }
}

QString getSaveFileName(QWidget *parent, const QString &caption,
                        const QString &dir,
                        const QString &filter,
                        QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if (dir.isEmpty()) // Default to user documents location
    {
        myDir = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation)[0];
    }
    else
    {
        myDir = dir;
    }
    QString result = QFileDialog::getSaveFileName(parent, caption, myDir, filter, &selectedFilter);

    /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
    QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
    QString selectedSuffix;
    if (filter_re.exactMatch(selectedFilter))
    {
        selectedSuffix = filter_re.cap(1);
    }

    /* Add suffix if needed */
    QFileInfo info(result);
    if (!result.isEmpty())
    {
        if (info.suffix().isEmpty() && !selectedSuffix.isEmpty())
        {
            /* No suffix specified, add selected suffix */
            if (!result.endsWith("."))
                result.append(".");
            result.append(selectedSuffix);
        }
    }

    /* Return selected suffix if asked to */
    if (selectedSuffixOut)
    {
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

Qt::ConnectionType blockingGUIThreadConnection()
{
    if (QThread::currentThread() != QCoreApplication::instance()->thread())
    {
        return Qt::BlockingQueuedConnection;
    }
    else
    {
        return Qt::DirectConnection;
    }
}

bool checkPoint(const QPoint &p, const QWidget *w)
{
    QWidget *atW = qApp->widgetAt(w->mapToGlobal(p));
    if (!atW)
        return false;
    return atW->topLevelWidget() == w;
}

bool isObscured(QWidget *w)
{
    return !(checkPoint(QPoint(0, 0), w) && checkPoint(QPoint(w->width() - 1, 0), w) && checkPoint(QPoint(0, w->height() - 1), w) && checkPoint(QPoint(w->width() - 1, w->height() - 1), w) && checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
}

void openDebugLogfile()
{
    boost::filesystem::path pathDebug = GetDataDir() / "debug.log";

    /* Open debug.log with the associated application */
    if (boost::filesystem::exists(pathDebug))
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(pathDebug.string())));
}

void openConfigfile()
{
    boost::filesystem::path pathConfig = GetConfigFile();

    /* Open privcy.conf with the associated application */
    if (boost::filesystem::exists(pathConfig))
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(pathConfig.string())));
}

ToolTipToRichTextFilter::ToolTipToRichTextFilter(int size_threshold, QObject *parent) : QObject(parent), size_threshold(size_threshold)
{
}

bool ToolTipToRichTextFilter::eventFilter(QObject *obj, QEvent *evt)
{
    if (evt->type() == QEvent::ToolTipChange)
    {
        QWidget *widget = static_cast<QWidget *>(obj);
        QString tooltip = widget->toolTip();
        if (tooltip.size() > size_threshold && !tooltip.startsWith("<qt>") && !Qt::mightBeRichText(tooltip))
        {
            // Prefix <qt/> to make sure Qt detects this as rich text
            // Escape the current message as HTML and replace \n by <br>
            tooltip = "<qt>" + HtmlEscape(tooltip, true) + "<qt/>";
            widget->setToolTip(tooltip);
            return true;
        }
    }
    return QObject::eventFilter(obj, evt);
}

void hyperlinks_slot1(){QString link= "https://discord.gg/b4mWcTm";QDesktopServices::openUrl(QUrl(link));}
void hyperlinks_slot2(){QString link= "https://privcy.eu/";QDesktopServices::openUrl(QUrl(link));}
void hyperlinks_slot3(){QString link= "https://github.com/privcycoin/privcy";QDesktopServices::openUrl(QUrl(link));}
//Exchanges
void hyperlinks2_slot1(){QString link= "https://crex24.com/exchange/PRiV-BTC";QDesktopServices::openUrl(QUrl(link));}
void hyperlinks2_slot2(){QString link= "https://frontend.coindeal.com/market/PRIV-BTC";QDesktopServices::openUrl(QUrl(link));}
void hyperlinks2_slot3(){QString link= "https://altmarkets.io/trading/privbtc";QDesktopServices::openUrl(QUrl(link));}


#ifdef WIN32
boost::filesystem::path static StartupShortcutPath()
{
    return GetSpecialFolderPath(CSIDL_STARTUP) / "PRiVCY.lnk";
}

bool GetStartOnSystemStartup()
{
    // check for Bitcoin.lnk
    return boost::filesystem::exists(StartupShortcutPath());
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    // If the shortcut exists already, remove it for updating
    boost::filesystem::remove(StartupShortcutPath());

    if (fAutoStart)
    {
        CoInitialize(NULL);

        // Get a pointer to the IShellLink interface.
        IShellLink *psl = NULL;
        HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL,
                                        CLSCTX_INPROC_SERVER, IID_IShellLink,
                                        reinterpret_cast<void **>(&psl));

        if (SUCCEEDED(hres))
        {
            // Get the current executable path
            TCHAR pszExePath[MAX_PATH];
            GetModuleFileName(NULL, pszExePath, sizeof(pszExePath));

            TCHAR pszArgs[5] = TEXT("-min");

            // Set the path to the shortcut target
            psl->SetPath(pszExePath);
            PathRemoveFileSpec(pszExePath);
            psl->SetWorkingDirectory(pszExePath);
            psl->SetShowCmd(SW_SHOWMINNOACTIVE);
            psl->SetArguments(pszArgs);

            // Query IShellLink for the IPersistFile interface for
            // saving the shortcut in persistent storage.
            IPersistFile *ppf = NULL;
            hres = psl->QueryInterface(IID_IPersistFile,
                                       reinterpret_cast<void **>(&ppf));
            if (SUCCEEDED(hres))
            {
                WCHAR pwsz[MAX_PATH];
                // Ensure that the string is ANSI.
                MultiByteToWideChar(CP_ACP, 0, StartupShortcutPath().string().c_str(), -1, pwsz, MAX_PATH);
                // Save the link by calling IPersistFile::Save.
                hres = ppf->Save(pwsz, TRUE);
                ppf->Release();
                psl->Release();
                CoUninitialize();
                return true;
            }
            psl->Release();
        }
        CoUninitialize();
        return false;
    }
    return true;
}

#elif defined(LINUX)

// Follow the Desktop Application Autostart Spec:
//  http://standards.freedesktop.org/autostart-spec/autostart-spec-latest.html

boost::filesystem::path static GetAutostartDir()
{
    namespace fs = boost::filesystem;

    char *pszConfigHome = getenv("XDG_CONFIG_HOME");
    if (pszConfigHome)
        return fs::path(pszConfigHome) / "autostart";
    char *pszHome = getenv("HOME");
    if (pszHome)
        return fs::path(pszHome) / ".config" / "autostart";
    return fs::path();
}

boost::filesystem::path static GetAutostartFilePath()
{
    return GetAutostartDir() / "PRiVCY.desktop";
}

bool GetStartOnSystemStartup()
{
    boost::filesystem::ifstream optionFile(GetAutostartFilePath());
    if (!optionFile.good())
        return false;
    // Scan through file for "Hidden=true":
    std::string line;
    while (!optionFile.eof())
    {
        getline(optionFile, line);
        if (line.find("Hidden") != std::string::npos &&
            line.find("true") != std::string::npos)
            return false;
    }
    optionFile.close();

    return true;
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    if (!fAutoStart)
        boost::filesystem::remove(GetAutostartFilePath());
    else
    {
        char pszExePath[MAX_PATH + 1];
        memset(pszExePath, 0, sizeof(pszExePath));
        if (readlink("/proc/self/exe", pszExePath, sizeof(pszExePath) - 1) == -1)
            return false;

        boost::filesystem::create_directories(GetAutostartDir());

        boost::filesystem::ofstream optionFile(GetAutostartFilePath(), std::ios_base::out | std::ios_base::trunc);
        if (!optionFile.good())
            return false;
        // Write a bitcoin.desktop file to the autostart directory:
        optionFile << "[Desktop Entry]\n";
        optionFile << "Type=Application\n";
        optionFile << "Name=PRiVCY\n";
        optionFile << "Exec=" << pszExePath << " -min\n";
        optionFile << "Terminal=false\n";
        optionFile << "Hidden=false\n";
        optionFile.close();
    }
    return true;
}
#else

// TODO: OSX startup stuff; see:
// https://developer.apple.com/library/mac/#documentation/MacOSX/Conceptual/BPSystemStartup/Articles/CustomLogin.html

bool GetStartOnSystemStartup() { return false; }
bool SetStartOnSystemStartup(bool fAutoStart) { return false; }

#endif

void setClipboard(const QString& str)
{
    QApplication::clipboard()->setText(str, QClipboard::Clipboard);
    QApplication::clipboard()->setText(str, QClipboard::Selection);
}

fs::path qstringToBoostPath(const QString &path)
{
    return fs::path(path.toStdString(), utf8);
}

QString boostPathToQString(const fs::path &path)
{
    return QString::fromStdString(path.string(utf8));
}

HelpMessageBox::HelpMessageBox(QWidget *parent) : QMessageBox(parent)
{
    header = tr("PRiVCY-Qt") + " " + tr("version") + " " +
             QString::fromStdString(FormatFullVersion()) + "\n\n" +
             tr("Usage:") + "\n" +
             "  PRiVCY-qt [" + tr("command-line options") + "]                     " + "\n";

    coreOptions = QString::fromStdString(HelpMessage());

    uiOptions = tr("UI options") + ":\n" +
                "  -lang=<lang>           " + tr("Set language, for example \"de_DE\" (default: system locale)") + "\n" +
                "  -min                   " + tr("Start minimized") + "\n" +
                "  -splash                " + tr("Show splash screen on startup (default: 1)") + "\n";

    setWindowTitle(tr("PRiVCY-Qt"));
    setTextFormat(Qt::PlainText);
    // setMinimumWidth is ignored for QMessageBox so put in non-breaking spaces to make it wider.
    setText(header + QString(QChar(0x2003)).repeated(50));
    setDetailedText(coreOptions + "\n" + uiOptions);
}

void HelpMessageBox::printToConsole()
{
    // On other operating systems, the expected action is to print the message to the console.
    QString strUsage = header + "\n" + coreOptions + "\n" + uiOptions;
    fprintf(stdout, "%s", strUsage.toStdString().c_str());
}

void HelpMessageBox::showOrPrint()
{
#if defined(WIN32)
    // On Windows, show a message box, as there is no stderr/stdout in windowed applications
    exec();
#else
    // On other operating systems, print help text to console
    printToConsole();
#endif
}

} // namespace GUIUtil
