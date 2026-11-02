// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <cstdio>
#include <cstring>
#include "../DSView/pv/appcontrol.h"
#include "../DSView/pv/mainframe.h"
#include "../DSView/pv/sigsession.h"
#include "../DSView/pv/storesession.h"
#include "../DSView/pv/config/appconfig.h"
#include "../DSView/pv/ui/langresource.h"
#include "../DSView/pv/log.h"
int main(int argc,char **argv) {
    if(argc!=3 || std::strcmp(argv[1],"--run-hardware")!=0) {
        std::fprintf(stderr,"Usage: hardware_export_capture_benchmark --run-hardware OUTPUT_DIRECTORY\n"
            "Opens a physical device, changes capture settings and starts two captures.\n");return 2;
    }
    const QDir output(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
    if(!QDir().mkpath(output.absolutePath()))return 2;
    const QString referencePath=output.filePath("hw-reference.csv");
    const QString concurrentPath=output.filePath("hw-concurrent.csv");
    if(QFileInfo::exists(referencePath) || QFileInfo::exists(concurrentPath)) {
        std::fprintf(stderr,"Benchmark output already exists; choose a new directory.\n");return 2;
    }
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName("DSView-hwbench");QApplication::setOrganizationName("DreamSourceLab");
    dsv_log_init();dsv_log_level(4);auto *control=AppControl::Instance();auto &cfg=AppConfig::Instance();cfg.LoadAll();
    LangResource::Instance()->Load(cfg.frameOptions.language);if(!control->Init()){std::puts("INIT FAILED");return 1;}
    pv::MainFrame window;control->Start();window.ShowFormInit();auto *session=control->GetSession();
    QTimer tick;tick.setInterval(10);QElapsedTimer clock;clock.start();QElapsedTimer deadline;deadline.start();int stage=0,result=0;bool observed_data_overlap=false;pv::StoreSession *store=nullptr;
    QObject::connect(&tick,&QTimer::timeout,&app,[&](){
        if(deadline.elapsed()>180000){std::printf("TIMEOUT stage=%d\n",stage);result=9;app.quit();return;}
        if(stage==0){
            if(!session->get_device()->is_hardware()){std::puts("NO HARDWARE SELECTED");result=1;app.quit();return;}
            bool rate_set=session->get_device()->set_config_uint64(SR_CONF_SAMPLERATE,10000000);
            bool limit_set=session->get_device()->set_config_uint64(SR_CONF_LIMIT_SAMPLES,150000000);
            std::printf("DEVICE=%s rate=%llu limit=%llu stream=%d rate_set=%d limit_set=%d\n",qPrintable(session->get_device()->name()),(unsigned long long)session->get_device()->get_sample_rate(),(unsigned long long)session->get_device()->get_sample_limit(),(int)session->get_device()->is_stream_mode(),rate_set,limit_set);std::fflush(stdout);
            if(!session->start_capture(false)){std::puts("FIRST START FAILED");result=2;app.quit();return;}
            clock.restart();stage=1;return;
        }
        if(stage==1 && clock.elapsed()>9000){
            bool stop=session->stop_capture();std::printf("FIRST STOP requested=%d ms=%lld\n",stop,(long long)clock.elapsed());std::fflush(stdout);stage=2;return;
        }
        if(stage==2){
            if(session->is_working() || session->get_device()->is_collecting())return;
            auto *old=session->get_snapshot(SR_CHANNEL_LOGIC);if(!old||old->empty()){std::puts("FIRST CAPTURE EMPTY");result=3;app.quit();return;}
            std::printf("FIRST samples=%llu\n",(unsigned long long)old->get_sample_count());std::fflush(stdout);
            cfg.appOptions.originalData=false;qunsetenv("DSVIEW_BENCH_LEGACY");
            store=new pv::StoreSession(session);store->SetExportTarget(referencePath,"csv");
            QObject::connect(store,&pv::StoreSession::progress_updated,&app,[](){},Qt::QueuedConnection);
            if(!store->export_start()){std::printf("REFERENCE ERROR %s\n",qPrintable(store->error()));result=4;app.quit();return;}stage=3;return;
        }
        if(stage==3 && !store->is_busy()){
            store->wait();QString error=store->error();delete store;store=nullptr;if(!error.isEmpty()){std::printf("REFERENCE FAILED %s\n",qPrintable(error));result=5;app.quit();return;}
            std::printf("REFERENCE bytes=%lld\n",(long long)QFileInfo(referencePath).size());std::fflush(stdout);
            qputenv("DSVIEW_BENCH_CHUNK_DELAY_MS","5");
            store=new pv::StoreSession(session);store->SetExportTarget(concurrentPath,"csv");
            QObject::connect(store,&pv::StoreSession::progress_updated,&app,[](){},Qt::QueuedConnection);
            if(!store->export_start()){std::printf("CONCURRENT EXPORT FAILED %s\n",qPrintable(store->error()));result=6;app.quit();return;}
            session->set_saving(true);
            for(int tries=0;tries<100 && store->is_busy() && !store->is_export_processing();++tries) QThread::usleep(100);
            stage=4; // start the next capture in this event turn

        }
        if(stage==4){
            if(!store->is_export_processing()) { if(!store->is_busy()){std::puts("EXPORT TOO FAST TO OVERLAP");result=7;app.quit();} return; }
            auto before=clock.nsecsElapsed();bool started=session->start_capture(false);bool overlap=store->is_export_processing();auto after=clock.nsecsElapsed();
            std::printf("SECOND start=%d processing_after_start=%d start_duration_ms=%.3f\n",started,overlap,(after-before)/1e6);std::fflush(stdout);
            if(!started){result=8;app.quit();return;}if(!overlap)result=10;clock.restart();stage=5;return;
        }
        if(stage==5){
            auto *next=session->get_snapshot(SR_CHANNEL_LOGIC);
            if(!observed_data_overlap && next && next->get_sample_count()>0 && store->is_export_processing()) {
                observed_data_overlap=true;
                std::printf("OVERLAP data_samples=%llu export_processing=1 elapsed_ms=%lld\n",(unsigned long long)next->get_sample_count(),(long long)clock.elapsed());std::fflush(stdout);
            }
            if(clock.elapsed()>3000){bool stop=session->stop_capture();std::printf("SECOND STOP requested=%d ms=%lld\n",stop,(long long)clock.elapsed());std::fflush(stdout);stage=6;}return;
        }
        if(stage==6){
            if(session->is_working()||session->get_device()->is_collecting()||store->is_busy())return;
            auto *next=session->get_snapshot(SR_CHANNEL_LOGIC);std::printf("SECOND samples=%llu\n",(unsigned long long)(next?next->get_sample_count():0));store->wait();auto err=store->error();
            std::printf("CONCURRENT bytes=%lld error=%s\n",(long long)QFileInfo(concurrentPath).size(),qPrintable(err));std::fflush(stdout);
            if(!next||next->empty()||!err.isEmpty()||!observed_data_overlap)result=11;delete store;store=nullptr;session->set_saving(false);stage=7;app.quit();return;
        }
    });
    tick.start();app.exec();tick.stop();if(session->is_working())session->stop_capture();if(store){store->cancel();store->wait();delete store;session->set_saving(false);}control->Stop();control->UnInit();dsv_log_uninit();return result;
}
