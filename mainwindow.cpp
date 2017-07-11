#include "customtabwidget.h"
#include "customtextedit.h"
#include "ui_mainwindow.h"
#include "mainwindow.h"
#include <QIODevice>
#include <QDesktopWidget>
#include <QPlainTextEdit>
#include <QFontMetrics>
#include <QApplication>
#include <QFontDatabase>
#include <QFontDialog>
#include <QInputDialog>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>
#include <QMimeData>
#include <QDialog>
#include <QDebug>
#include <QEvent>
#include <QMap>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    giCurrentTabIndex = 0;
    giCurrentFileIndex = 0;
    giTotalTabs = 0;
    giTabCharacters = 4;
    giTimerDelay = 250;
    gsThemeFile = "Default";
    gbIsOpenedFile = false;
    gbSaveCancelled = false;
    gbIsReloadFile = false;
    gbIsAutoreloadEnabled = false;
    gobSearchDialog = new SearchDialog(this);
    worker = new Worker;
    workerThread = new QThread;
    setAcceptDrops(true);
    loadConfig();
    gobTimer = new QTimer(this);
    gobMovie->setScaledSize(QSize(15,15));
    gsDefaultDir = QDir::homePath();
    giDefaultDirCounter = 0;

    this->move(QApplication::desktop()->screen()->rect().center() - this->rect().center());

    connect(ui->tabWidget,SIGNAL(currentChanged(int)),this,SLOT(main_slot_tabChanged(int)));
    connect(ui->tabWidget,SIGNAL(ctw_signal_tabMoved(int,int)),this,SLOT(main_slot_tabMoved(int,int)));
    connect(this,SIGNAL(main_signal_setTextEdit(QPlainTextEdit*)),gobSearchDialog,SLOT(search_slot_setTextEdit(QPlainTextEdit*)));
    connect(gobSearchDialog,SIGNAL(search_signal_getTextEditText()),this,SLOT(main_slot_getTextEditText()));
    connect(gobSearchDialog,SIGNAL(search_signal_resetCursor()),this,SLOT(main_slot_resetCursor()));
    connect(this,SIGNAL(main_signal_loadFile(QFile*)),worker,SLOT(worker_slot_loadFile(QFile*)));
    connect(worker,SIGNAL(worker_signal_appendText(QString)),this,SLOT(main_slot_appendText(QString)));
    connect(gobTimer, SIGNAL(timeout()), this, SLOT(on_actionReload_file_triggered()));
    connect(workerThread,SIGNAL(finished()),worker,SLOT(deleteLater()));
    connect(workerThread,SIGNAL(finished()),workerThread,SLOT(deleteLater()));
    worker->moveToThread(workerThread);
    workerThread->start();
    on_actionNew_Tab_triggered();
}

MainWindow::~MainWindow()
{
    delete ui;
    workerThread->quit();
    workerThread->wait();
}

void MainWindow::on_actionOpen_triggered()
{
    QString lsFileName = "";

    giDefaultDirCounter ++;

    if(giDefaultDirCounter > 1) {
        gsDefaultDir = "";
        giDefaultDirCounter = 2;
    }

    giCurrentFileIndex = 0;

    if(!gobFileNames.isEmpty()){
        //qDebug() << "Reading array, giCurrentFileIndex = " << giCurrentFileIndex;
        lsFileName = gobFileNames.at(giCurrentFileIndex);
        loadFile(lsFileName);
    }else {
        gobFileNames = QFileDialog::getOpenFileNames(this
                                              ,"Open File"
                                              ,gsDefaultDir
                                              ,tr("All Files (*);;Text Files (*.txt);;Log files (*.log)"));
        lsFileName = gobFileNames.at(giCurrentFileIndex);
        loadFile(lsFileName);
    }
}

bool MainWindow::on_actionSave_As_triggered()
{
    QString lsFileName;

    lsFileName = QFileDialog::getSaveFileName(this
                                              ,"Save File"
                                              ,""
                                              ,tr("Text Files (*.txt);;All Files (*)"));

    giCurrentTabIndex = ui->tabWidget->currentIndex();
    if(gobHash.value(giCurrentTabIndex) != lsFileName){
        gobHash.insert(giCurrentTabIndex,lsFileName);
    }

    QPlainTextEdit* edit = qobject_cast<QPlainTextEdit*>(ui->tabWidget->widget(giCurrentTabIndex));

    if(!saveFile(lsFileName,edit->toPlainText())) return false;

    ui->indicatorLabel->clear();
    gobIsModifiedTextHash.insert(giCurrentTabIndex,false);
    setCurrentTabNameFromFile(lsFileName);
    ui->statusBar->setText(lsFileName);
    return true;
}

bool MainWindow::on_actionSave_triggered()
{
    QString lsFileName;

    giCurrentTabIndex = ui->tabWidget->currentIndex();
    lsFileName = gobHash.value(giCurrentTabIndex);

    QFile file(lsFileName);

    if(file.exists()){

        QPlainTextEdit* edit = qobject_cast<QPlainTextEdit*>(ui->tabWidget->widget(giCurrentTabIndex));
        if(!saveFile(lsFileName,edit->toPlainText())) return false;
        ui->indicatorLabel->clear();
        gobIsModifiedTextHash.insert(giCurrentTabIndex,false);
        setCurrentTabNameFromFile(lsFileName);

    }else{
        if(!on_actionSave_As_triggered()) return false;
    }

    return true;
}

bool MainWindow::saveFile(QString asFileName, QString asText)
{
    QFile file(asFileName);

    if(asFileName.isEmpty()) return false;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        QMessageBox::critical(this,"Error","File could not be opened");
        return false;
    }

    QTextStream out(&file);
    out << asText;
    file.close();

    return true;
}

bool MainWindow::saveConfig()
{

    QString configText = gsThemeFile + "@@"
            + gobCurrentPlainTextEdit->fontInfo().family() + "@@"
            + QString::number(gobCurrentPlainTextEdit->fontInfo().style()) + "@@"
            + QString::number(gobCurrentPlainTextEdit->fontInfo().pointSize());
    if(!saveFile("config.ini",configText)) return false;
    return true;
}

bool MainWindow::loadConfig()
{
    QString line;
    QStringList lobValues;
    QFile *lobConfigFile = new QFile("config.ini");
    if(!lobConfigFile->open(QFile::ReadOnly)){

        gsSavedFont = "Arial";
        giSavedFontStyle = 0;
        giSavedFontPointSize = 10;

        return false;
    }

    QTextStream lobInputStream(lobConfigFile);
    while (!lobInputStream.atEnd()) {
      line = lobInputStream.readLine();
    }

    lobInputStream.flush();
    lobConfigFile->close();

    lobValues = line.split("@@");

    if(lobValues.length() >= 4){
        gsSavedFont = line.split("@@").at(1);
        giSavedFontStyle = line.split("@@").at(2).toInt();
        giSavedFontPointSize = line.split("@@").at(3).toInt();
    }

    if(!line.isEmpty() && line != ""){
        QFile style(line.split("@@").at(0));
        if(style.exists() && style.open(QFile::ReadOnly)){
            QString styleContents = QLatin1String(style.readAll());
            style.close();
            this->setStyleSheet(styleContents);
        }
    }


    return true;
}

void MainWindow::on_actionAbout_QNote_triggered()
{
    QMessageBox::about(this,"QNote 1.0.0",
                       "<style>"
                       "a:link {"
                           "color: orange;"
                           "background-color: transparent;"
                           "text-decoration: none;"
                       "}"
                       "</style>"
                       "<p>Simple text editor"
                       "<p> Jaime A. Quiroga P."
                       "<p><a href='http://www.gtronick.com'>GTRONICK</a>");
}

void MainWindow::on_actionAbout_QT_triggered()
{
    QMessageBox::aboutQt(this,"About QT5.8");
}

void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    checkIfUnsaved(index);
}

void MainWindow::on_actionClose_Tab_triggered()
{
    checkIfUnsaved(giCurrentTabIndex);
}

void MainWindow::on_actionNew_Tab_triggered()
{
    giTotalTabs ++;
    QApplication::processEvents();
    CustomTextEdit *lobPlainTexEdit = new CustomTextEdit();
    lobPlainTexEdit->setPlaceholderText("Type Here...");
    lobPlainTexEdit->setFrameShape(QFrame::NoFrame);
    lobPlainTexEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    lobPlainTexEdit->setAcceptDrops(true);
    int fontWidth = QFontMetrics(lobPlainTexEdit->currentCharFormat().font()).averageCharWidth();
    lobPlainTexEdit->setTabStopWidth( giTabCharacters * fontWidth );
    QPalette p = lobPlainTexEdit->palette();
    p.setColor(QPalette::Highlight, QColor(qRgb(200,0,0)));
    p.setColor(QPalette::HighlightedText, QColor(qRgb(255,255,255)));
    lobPlainTexEdit->setPalette(p);
    connect(lobPlainTexEdit,SIGNAL(textChanged()),this,SLOT(main_slot_textChanged()));
    connect(lobPlainTexEdit,SIGNAL(customTextEdit_signal_processDropEvent(QDropEvent*)),this,SLOT(main_slot_processDropEvent(QDropEvent*)));
    connect(lobPlainTexEdit,SIGNAL(cursorPositionChanged()),this,SLOT(main_slot_currentLineChanged()));
    connect(this,SIGNAL(main_signal_refreshHighlight()),lobPlainTexEdit,SLOT(highlightCurrentLine()));
    connect(gobSearchDialog,SIGNAL(search_signal_disableHighLight()),lobPlainTexEdit,SLOT(cte_slot_disableHighLight()));
    connect(gobSearchDialog,SIGNAL(search_signal_enableHighLight()),lobPlainTexEdit,SLOT(cte_slot_enableHighLight()));
    QFont serifFont(gsSavedFont, giSavedFontPointSize, giSavedFontStyle);
    lobPlainTexEdit->setFont(serifFont);
    giCurrentTabIndex = this->ui->tabWidget->addTab(lobPlainTexEdit,"New " + QString::number(giTotalTabs));
    this->ui->tabWidget->setCurrentIndex(giCurrentTabIndex);
    gobHash.insert(giCurrentTabIndex,"");
    gobIsModifiedTextHash.insert(giCurrentTabIndex,false);


}

void MainWindow::on_actionReload_file_triggered()
{
    gbIsReloadFile = true;
    if(!gbIsAutoreloadEnabled) checkIfUnsaved(giCurrentTabIndex);
    if(gbSaveCancelled == false){
        QString lsFileName = gobHash.value(giCurrentTabIndex);
        if(!lsFileName.isEmpty()){
            gobFile = new QFile(lsFileName);
            setCurrentTabNameFromFile(lsFileName);
            if(gobFile->exists()){
                if(!gobFile->open(QIODevice::ReadOnly | QIODevice::Text)){
                    QMessageBox::critical(this,"Error","File could not be opened");
                    return;
                }
                emit main_signal_loadFile(gobFile);
            }
        }else{
            gbIsReloadFile = false;
        }
    }else{
        gbSaveCancelled = false;
    }
}

void MainWindow::on_actionGo_to_line_triggered()
{
    bool ok;
        int line_number = QInputDialog::getInt(this, tr("Go to"),
                                               tr("Go to line: "),
                                               1,
                                               1,
                                               gobCurrentPlainTextEdit->blockCount(),
                                               1,
                                               &ok);
    if(ok){
        int moves = line_number-(gobCurrentPlainTextEdit->textCursor().blockNumber() + 1);
        QTextCursor cursor = gobCurrentPlainTextEdit->textCursor();
        if(moves)
        {
            if(moves<0) cursor.movePosition(QTextCursor::Up, QTextCursor::MoveAnchor, -moves);
            else cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, moves);

            cursor.movePosition(QTextCursor::StartOfLine);

            gobCurrentPlainTextEdit->setTextCursor(cursor);
        }
    }
}

void MainWindow::on_actionFind_Replace_triggered()
{
    gobSearchDialog->setVisible(true);
}

void MainWindow::main_slot_getTextEditText()
{
    QPlainTextEdit* edit = qobject_cast<QPlainTextEdit*>(ui->tabWidget->widget(giCurrentTabIndex));
    emit main_signal_setTextEdit(edit);
}

void MainWindow::main_slot_resetCursor()
{
    QPlainTextEdit* edit = qobject_cast<CustomTextEdit*>(ui->tabWidget->widget(giCurrentTabIndex));
    edit->moveCursor(QTextCursor::Start);
}

void MainWindow::main_slot_tabChanged(int aIndex)
{
    giCurrentTabIndex = aIndex;
    this->ui->statusBar->setText(gobHash.value(aIndex));
    gobCurrentPlainTextEdit = qobject_cast<CustomTextEdit*>(ui->tabWidget->widget(giCurrentTabIndex));
    QFont serifFont(gsSavedFont, giSavedFontPointSize, giSavedFontStyle);
    gobCurrentPlainTextEdit->setFont(serifFont);
    if(gobIsModifiedTextHash.value(aIndex)){
        ui->indicatorLabel->setPixmap(QPixmap("://unsaved.png"));
    }else{
        if(!gbIsAutoreloadEnabled) ui->indicatorLabel->clear();
    }
}

void MainWindow::main_slot_tabMoved(int from, int to)
{
    QString lsTemporalValue = "";

    lsTemporalValue = gobHash.value(to);
    gobHash.insert(to,gobHash.value(from));
    gobHash.insert(from,lsTemporalValue);

    bool lbTemporalValue = gobIsModifiedTextHash.value(to);
    gobIsModifiedTextHash.insert(to,gobIsModifiedTextHash.value(from));
    gobIsModifiedTextHash.insert(from,lbTemporalValue);
}

void MainWindow::main_slot_textChanged()
{
    if(gbIsOpenedFile == false && gobIsModifiedTextHash.value(giCurrentTabIndex) == false && !gbIsAutoreloadEnabled){
        ui->indicatorLabel->setPixmap(QPixmap("://unsaved.png"));
        ui->indicatorLabel->setToolTip("Document unsaved");
        gobIsModifiedTextHash.insert(giCurrentTabIndex,true);
    }
}

void MainWindow::main_slot_appendText(QString asText)
{
    QString lsFileName;

    gbIsOpenedFile = true;
    gobCurrentPlainTextEdit->clear();
    gobCurrentPlainTextEdit->appendPlainText(asText);
    gobIsModifiedTextHash.insert(giCurrentTabIndex,false);
    gbIsOpenedFile = false;
    gobFile->close();

    if(gbIsReloadFile){
        gbIsReloadFile = false;
    }else{
        main_slot_resetCursor();
    }

    //Se carga el siguiente archivo seleccionado
    giCurrentFileIndex ++;
    if(giCurrentFileIndex < gobFileNames.length()){
        lsFileName = gobFileNames.at(giCurrentFileIndex);
        loadFile(lsFileName);
    }else{
        giCurrentFileIndex = 0;
        gobFileNames.clear();
    }
}

void MainWindow::main_slot_processDropEvent(QDropEvent *event)
{
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasUrls()){
        gobFileNames.clear();
        giCurrentFileIndex = 0;
        QList<QUrl> urlList = mimeData->urls();

        for (int i = 0; i < urlList.size(); i++)
        {
            gobFileNames.append(urlList.at(i).toLocalFile());
        }

        loadFile(gobFileNames.at(0));
        event->acceptProposedAction();
    }
}

void MainWindow::setCurrentTabNameFromFile(QString asFileName)
{
    QString extension = "";
    QString fileName = "";

    if(!QFileInfo(asFileName).completeSuffix().isEmpty()){
            extension = "."+QFileInfo(asFileName).completeSuffix();
        }

    fileName = QFileInfo(asFileName).baseName() + extension;

    ui->tabWidget->setTabText(ui->tabWidget->currentIndex(),fileName);
}

void MainWindow::checkIfUnsaved(int index)
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Warning!");
    msgBox.setInformativeText("Do you want to save your changes?");
    msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Save);

    if(gobIsModifiedTextHash.value(index) == true){
        msgBox.setText(ui->tabWidget->tabText(index) + " has been modified.");
        int ret = msgBox.exec();

        switch (ret) {
          case QMessageBox::Save:
              if(!gbIsReloadFile && on_actionSave_triggered()) closeTab(index);
              else{
                  ui->indicatorLabel->clear();
                  on_actionSave_triggered();
              }
              break;
          case QMessageBox::Discard:
              if(!gbIsReloadFile) closeTab(index);
              else{
                  ui->indicatorLabel->clear();
              }
              break;
          case QMessageBox::Cancel:
              gbSaveCancelled = true;
              break;
          default:
              on_actionSave_triggered();
              if(!gbIsReloadFile) closeTab(index);
              break;
        }
    }else{
        if(!gbIsReloadFile) closeTab(index);
    }
}

void MainWindow::closeTab(int index)
{
    QFile *lobFile;

    if(giTotalTabs > 1){
        lobFile = new QFile(gobHash.value(index));
        giTotalTabs --;

        this->ui->tabWidget->removeTab(index);
        gobHash.remove(index);
        for(int i = index; i < giTotalTabs; i ++){
            gobHash.insert(i,gobHash.value(i + 1));
        }

        for(int i = index; i < giTotalTabs; i ++){
            gobIsModifiedTextHash.insert(i,gobIsModifiedTextHash.value(i + 1));
        }

        ui->statusBar->setText(gobHash.value(giCurrentTabIndex));
        if(lobFile->isOpen()){
            lobFile->flush();
            lobFile->~QFile();
        }
    }else{
        if(!saveConfig()){
            QMessageBox::critical(this,"Warning!","The config file could not be saved");
        }
        QApplication::quit();
    }
}

void MainWindow::loadFile(QString asFileName)
{
    //qDebug() << "Begin loadFile, asFileName: " << asFileName;
    if(!asFileName.isEmpty()){
        gobFile = new QFile(asFileName);

        if(gobFile->exists()){
            on_actionNew_Tab_triggered();
            gobHash.insert(giCurrentTabIndex,asFileName);
            setCurrentTabNameFromFile(asFileName);
            main_slot_tabChanged(giCurrentTabIndex);

            if(!gobFile->open(QIODevice::ReadOnly | QIODevice::Text)){
                QMessageBox::critical(this,"Error","File could not be opened");
                return;
            }

            gobCurrentPlainTextEdit = qobject_cast<CustomTextEdit*>(ui->tabWidget->widget(giCurrentTabIndex));
            emit(main_signal_loadFile(gobFile));

        }else{
            QMessageBox::warning(this,"File not found","The file " + asFileName + " does not exist.");
        }
    }
    //qDebug() << "End loadFile";
}

void MainWindow::setFileNameFromCommandLine(QStringList asFileNames)
{
    //qDebug() << "Begin setFileNameFromCommandLine, asFileNames: " << asFileNames;
    gobFileNames = asFileNames;
    this->on_actionOpen_triggered();
    //qDebug() << "End setFileNameFromCommandLine";
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    bool lbClose = false;

    for(int i = giTotalTabs - 1; i >= 0; i --){
        checkIfUnsaved(i);
        if(gbSaveCancelled){
            i = 0;
            gbSaveCancelled = false;
        }
    }
    if(lbClose){
        event->accept();
    }else{
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasUrls()){

        QList<QUrl> urlList = mimeData->urls();

        for (int i = 0; i < urlList.size(); i++)
        {
            gobCurrentPlainTextEdit->appendPlainText(urlList.at(i).toLocalFile());
        }

        event->acceptProposedAction();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::on_actionWord_wrap_toggled(bool arg1)
{
    if(arg1){
        gobCurrentPlainTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    }else{
        gobCurrentPlainTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
}

void MainWindow::on_actionLoad_theme_triggered()
{
    gsThemeFile = QFileDialog::getOpenFileName(this, tr("Open Style"),
                                                        "",
                                                        tr("Style sheets (*.qss);;All files(*.*)"));

    if(gsThemeFile != NULL && gsThemeFile != ""){
        QFile styleFile(gsThemeFile);
        styleFile.open(QFile::ReadOnly);
        QString StyleSheet = QLatin1String(styleFile.readAll());
        this->setStyleSheet(StyleSheet);
    }

    emit main_signal_refreshHighlight();
}

void MainWindow::on_actionSystem_theme_triggered()
{
    this->setStyleSheet("");
    emit main_signal_refreshHighlight();
}


void MainWindow::on_actionAuto_Reload_tail_f_toggled(bool arg1)
{
    gbIsAutoreloadEnabled = arg1;

    if(arg1){
        ui->indicatorLabel->setToolTip("Auto Reload Active");
        this->ui->indicatorLabel->setMovie(gobMovie);
        gobTimer->start(giTimerDelay);
        ui->indicatorLabel->movie()->start();
    }else{
        ui->indicatorLabel->setToolTip("");
        gobTimer->stop();
        ui->indicatorLabel->movie()->stop();
        ui->indicatorLabel->clear();
    }
}

void MainWindow::on_actionAuto_Reload_delay_triggered()
{
    bool lbOk;
    int liDelay = QInputDialog::getInt(this, tr("Auto Reload delay (ms)"),
                                 tr("milliseconds:"), 250, 100, 5000, 1, &lbOk);
    if (lbOk)
        giTimerDelay = liDelay;
}

void MainWindow::main_slot_currentLineChanged()
{
    int liLine = gobCurrentPlainTextEdit->textCursor().blockNumber() + 1;
    int liCol = gobCurrentPlainTextEdit->textCursor().columnNumber();
    ui->lineNumberLabel->setText(QString("Line: %1, Col: %2").arg(liLine).arg(liCol));
}

void MainWindow::on_actionFont_triggered()
{
    bool ok;
    QFont font = QFontDialog::getFont(
                    &ok, QFont("Helvetica [Cronyx]", 10), this);
    if (ok) {
        gobCurrentPlainTextEdit->setFont(font);
        giSavedFontPointSize = gobCurrentPlainTextEdit->fontInfo().pointSize();
        giSavedFontStyle = gobCurrentPlainTextEdit->fontInfo().style();
        gsSavedFont = gobCurrentPlainTextEdit->fontInfo().family();
    }
}

void MainWindow::on_actionAlways_on_top_triggered(bool checked)
{
    Qt::WindowFlags flags = this->windowFlags();
    if (checked)
    {
        this->setWindowFlags(flags | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint);
        this->show();
    }
    else
    {
        this->setWindowFlags(flags ^ (Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint));
        this->show();
    }
}