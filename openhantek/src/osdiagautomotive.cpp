// SPDX-License-Identifier: GPL-2.0-or-later
//
// OSDiag Ingenieria Automotriz - automotive extension for OpenHantek6022
// Adds the menu "Automotriz" with ready-to-use measurement presets
// (built-in presets from ":/osdiag" + user presets from Documents/OSDiag/presets)
// and a dock that shows the connection guide of the selected test.

#include "mainwindow.h"
#include "src/ui_mainwindow.h"

#include "dsosettings.h"
#include "dsowidget.h"
#include "hantekdso/controlspecification.h"

#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>


static QString osdiagUserPresetDir() {
    return QStandardPaths::writableLocation( QStandardPaths::DocumentsLocation ) + "/OSDiag/presets";
}


// read the optional [OSDiag] section (title + connection guide) of a preset file
static void readOsdiagMeta( const QString &fileName, QString &title, QString &guide ) {
    QSettings meta( fileName, QSettings::IniFormat );
#if ( QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 ) )
    meta.setIniCodec( "UTF-8" );
#endif
    title = meta.value( "OSDiag/title", QFileInfo( fileName ).completeBaseName() ).toString();
    guide = meta.value( "OSDiag/guide" ).toString();
}


void MainWindow::applyOsdiagPreset( const QString &fileName, const Dso::ControlSpecification *spec ) {
    QString title, guide;
    readOsdiagMeta( fileName, title, guide );

    // keep the current window layout, a preset only changes the scope settings
    const QByteArray geometry = saveGeometry();
    const QByteArray state = saveState();
    if ( !dsoSettings->loadFromFile( fileName ) ) {
        QMessageBox::warning( this, tr( "OSDiag" ), tr( "No se pudo cargar el preset:\n%1" ).arg( fileName ) );
        return;
    }
    dsoSettings->mainWindowGeometry = geometry;
    dsoSettings->mainWindowState = state;

    // same update sequence as "File -> Open"
    emit settingsLoaded( &dsoSettings->scope, spec );
    dsoWidget->updateTimebase( dsoSettings->scope.horizontal.timebase );
    for ( ChannelID channel = 0; channel < spec->channels; ++channel ) {
        dsoWidget->updateVoltageUsed( channel, dsoSettings->scope.voltage[ channel ].used );
        dsoWidget->updateSpectrumUsed( channel, dsoSettings->scope.spectrum[ channel ].used );
    }

    osdiagGuideLabel->setText( QString( "<b>%1</b><br/><br/>%2" )
                                   .arg( title.toHtmlEscaped(),
                                         guide.isEmpty() ? tr( "Preset sin guia de conexion." )
                                                         : guide.toHtmlEscaped().replace( "\n", "<br/>" ) ) );
    osdiagGuideDock->show();
    osdiagGuideDock->raise();
    statusBar()->showMessage( tr( "Preset OSDiag: %1" ).arg( title ), 5000 );
}


void MainWindow::setupOsdiagAutomotive( const Dso::ControlSpecification *spec ) {
    // connection guide dock
    osdiagGuideDock = new QDockWidget( tr( "Guia OSDiag" ), this );
    osdiagGuideDock->setObjectName( "OSDiagGuideDock" );
    osdiagGuideLabel = new QLabel();
    osdiagGuideLabel->setWordWrap( true );
    osdiagGuideLabel->setTextFormat( Qt::RichText );
    osdiagGuideLabel->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    osdiagGuideLabel->setMargin( 6 );
    osdiagGuideLabel->setText( tr( "<b>OSDiag Ingenieria Automotriz</b><br/><br/>"
                                   "Elige una prueba en el menu <i>Automotriz</i>: el osciloscopio se configura solo "
                                   "y aqui aparece como conectar las puntas." ) );
    // OSDiag logo on top of the guide text
    QWidget *guideWidget = new QWidget( osdiagGuideDock );
    QHBoxLayout *guideLayout = new QHBoxLayout( guideWidget );
    guideLayout->setContentsMargins( 4, 4, 4, 4 );
    QLabel *logoLabel = new QLabel( guideWidget );
    logoLabel->setPixmap( QPixmap( ":/osdiag/logo.png" ).scaledToHeight( 56, Qt::SmoothTransformation ) );
    logoLabel->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    guideLayout->addWidget( logoLabel, 0, Qt::AlignTop );
    guideLayout->addWidget( osdiagGuideLabel, 1 );
    osdiagGuideDock->setWidget( guideWidget );
    // window icon: the "OS" part of the logo on a rounded badge
    {
        const QPixmap logo( ":/osdiag/logo.png" );
        const QImage logoImage = logo.toImage();
        QPixmap icon( 128, 128 );
        icon.fill( Qt::transparent );
        QPainter painter( &icon );
        painter.setRenderHint( QPainter::Antialiasing );
        painter.setRenderHint( QPainter::SmoothPixmapTransform );
        painter.setPen( Qt::NoPen );
        painter.setBrush( logoImage.pixelColor( 80, 2 ) ); // background color of the logo around "OS"
        painter.drawRoundedRect( 0, 0, 128, 128, 22, 22 );
        const QPixmap os = logo.copy( 3, 4, 157, 58 ).scaledToWidth( 118, Qt::SmoothTransformation );
        painter.drawPixmap( ( 128 - os.width() ) / 2, ( 128 - os.height() ) / 2, os );
        painter.end();
        setWindowIcon( QIcon( icon ) );
    }
    addDockWidget( Qt::BottomDockWidgetArea, osdiagGuideDock );
    if ( ui->menuView )
        ui->menuView->addAction( osdiagGuideDock->toggleViewAction() );

    // menu "Automotriz" between "Oscilloscope" and "Help"
    QMenu *menuAuto = new QMenu( tr( "&Automotriz" ), this );
    menuAuto->setToolTipsVisible( true );
    ui->menubar->insertMenu( ui->menuHelp->menuAction(), menuAuto );
    deviceCommandActions << menuAuto->menuAction(); // like "File -> Open": only when the device is ready

    auto addPresets = [ this, spec ]( QMenu *menu, const QString &dirPath ) {
        QDir dir( dirPath );
        const QStringList files = dir.entryList( QStringList() << "*.conf", QDir::Files, QDir::Name );
        for ( const QString &file : files ) {
            const QString path = dir.filePath( file );
            QString title, guide;
            readOsdiagMeta( path, title, guide );
            QAction *action = menu->addAction( title );
            action->setToolTip( guide );
            connect( action, &QAction::triggered, this, [ this, path, spec ]() { applyOsdiagPreset( path, spec ); } );
        }
        return files.size();
    };

    addPresets( menuAuto, ":/osdiag" );
    menuAuto->addSeparator();

    // user presets, re-read each time the submenu opens so new files appear without restart
    QMenu *menuUser = menuAuto->addMenu( tr( "Mis presets" ) );
    menuUser->setToolTipsVisible( true );
    connect( menuUser, &QMenu::aboutToShow, this, [ this, menuUser, addPresets ]() {
        menuUser->clear();
        if ( addPresets( menuUser, osdiagUserPresetDir() ) == 0 )
            menuUser->addAction( tr( "(vacio)" ) )->setEnabled( false );
    } );

    QAction *actionSavePreset = menuAuto->addAction( tr( "Guardar configuracion actual como preset..." ) );
    connect( actionSavePreset, &QAction::triggered, this, [ this ]() {
        bool ok = false;
        const QString title = QInputDialog::getText( this, tr( "Guardar preset OSDiag" ), tr( "Nombre de la prueba:" ),
                                                     QLineEdit::Normal, QString(), &ok )
                                  .trimmed();
        if ( !ok || title.isEmpty() )
            return;
        const QString guide = QInputDialog::getMultiLineText( this, tr( "Guardar preset OSDiag" ),
                                                              tr( "Guia de conexion (opcional):" ), QString(), &ok )
                                  .trimmed();
        QDir().mkpath( osdiagUserPresetDir() );
        QString base = title;
        base.replace( QRegularExpression( "[^A-Za-z0-9_-]+" ), "_" );
        const QString path = osdiagUserPresetDir() + "/" + base + ".conf";
        dsoSettings->mainWindowGeometry = saveGeometry();
        dsoSettings->mainWindowState = saveState();
        if ( !dsoSettings->saveToFile( path ) ) {
            QMessageBox::warning( this, tr( "OSDiag" ), tr( "No se pudo guardar:\n%1" ).arg( path ) );
            return;
        }
        QSettings meta( path, QSettings::IniFormat );
#if ( QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 ) )
        meta.setIniCodec( "UTF-8" );
#endif
        meta.setValue( "OSDiag/title", title );
        meta.setValue( "OSDiag/guide", guide );
        meta.sync();
        statusBar()->showMessage( tr( "Preset guardado: %1" ).arg( path ), 5000 );
    } );

    QAction *actionOpenDir = menuAuto->addAction( tr( "Abrir carpeta de mis presets" ) );
    connect( actionOpenDir, &QAction::triggered, this, []() {
        QDir().mkpath( osdiagUserPresetDir() );
        QDesktopServices::openUrl( QUrl::fromLocalFile( osdiagUserPresetDir() ) );
    } );
}
