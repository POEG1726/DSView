// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <cstdio>
#include "../DSView/pv/appcontrol.h"
#include "../DSView/pv/mainframe.h"
#include "../DSView/pv/sigsession.h"
#include "../DSView/pv/storesession.h"
#include "../DSView/pv/config/appconfig.h"
#include "../DSView/pv/ui/langresource.h"
#include "../DSView/pv/log.h"
int main(int argc, char **argv) {
    if(argc!=3) {
        std::fprintf(stderr,"Usage: export_actual_benchmark INPUT_DSL OUTPUT_DIRECTORY\n");return 2;
    }
    const QString input=QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QDir output(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
    if(!QFileInfo(input).isFile() || !QDir().mkpath(output.absolutePath()))return 2;
    const QString legacyPath=output.filePath("benchmark-legacy.csv");
    const QString fastPath=output.filePath("benchmark-fast.csv");
    if(QFileInfo::exists(legacyPath) || QFileInfo::exists(fastPath)) {
        std::fprintf(stderr,"Benchmark output already exists; choose a new directory.\n");return 2;
    }
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName("DSView-bench"); QApplication::setOrganizationName("DreamSourceLab");
    dsv_log_init(); dsv_log_level(4);
    auto *control=AppControl::Instance(); auto &config=AppConfig::Instance(); config.LoadAll();
    LangResource::Instance()->Load(config.frameOptions.language);
    if (!control->Init()) { std::fprintf(stderr,"INIT FAILED\n"); return 1; }
    control->_open_file_name=input.toUtf8().toStdString();
    pv::MainFrame window; control->Start(); window.ShowFormInit();
    auto *session=control->GetSession();
    pv::StoreSession *store=nullptr; QElapsedTimer timer; QElapsedTimer deadline; deadline.start();
    int stage=0; int result=0;
    QTimer tick; tick.setInterval(20);
    QObject::connect(&tick,&QTimer::timeout,&app,[&](){
        if(deadline.elapsed()>600000) { std::fprintf(stderr,"TIMEOUT stage=%d\n",stage); result=9; app.quit(); return; }
        if(stage==0) {
            auto *snapshot=session->get_snapshot(SR_CHANNEL_LOGIC);
            if(!snapshot || snapshot->empty() || session->is_working() || session->get_device()->is_collecting()) return;
            std::printf("READY samples=%llu rate=%llu\n",(unsigned long long)snapshot->get_sample_count(),(unsigned long long)session->cur_snap_samplerate()); std::fflush(stdout);
            config.appOptions.originalData=false; qputenv("DSVIEW_BENCH_LEGACY","1");
            store=new pv::StoreSession(session); store->SetExportTarget(legacyPath,"csv");
            QObject::connect(store,&pv::StoreSession::progress_updated,&app,[](){},Qt::QueuedConnection);
            timer.start(); if(!store->export_start()){std::fprintf(stderr,"LEGACY START FAILED %s\n",qPrintable(store->error())); result=2;app.quit();return;}
            stage=1; return;
        }
        if(stage==1 && !store->is_busy()) {
            auto ms=timer.elapsed(); store->wait(); QString err=store->error(); delete store; store=nullptr;
            if(!err.isEmpty()){std::fprintf(stderr,"LEGACY FAILED %s\n",qPrintable(err));result=3;app.quit();return;}
            std::printf("LEGACY ms=%lld bytes=%lld\n",(long long)ms,(long long)QFileInfo(legacyPath).size());std::fflush(stdout);
            qunsetenv("DSVIEW_BENCH_LEGACY"); store=new pv::StoreSession(session); store->SetExportTarget(fastPath,"csv");
            QObject::connect(store,&pv::StoreSession::progress_updated,&app,[](){},Qt::QueuedConnection);
            timer.restart(); if(!store->export_start()){std::fprintf(stderr,"FAST START FAILED %s\n",qPrintable(store->error()));result=4;app.quit();return;}
            stage=2; return;
        }
        if(stage==2 && !store->is_busy()) {
            auto ms=timer.elapsed();store->wait();QString err=store->error();delete store;store=nullptr;
            if(!err.isEmpty()){std::fprintf(stderr,"FAST FAILED %s\n",qPrintable(err));result=5;app.quit();return;}
            std::printf("FAST ms=%lld bytes=%lld\n",(long long)ms,(long long)QFileInfo(fastPath).size());std::fflush(stdout);
            stage=3;app.quit();return;
        }
    });
    tick.start(); app.exec(); tick.stop(); if(store){store->cancel();store->wait();delete store;}
    control->Stop(); control->UnInit(); dsv_log_uninit(); return result;
}
