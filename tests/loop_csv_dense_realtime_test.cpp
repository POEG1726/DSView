// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../DSView/pv/data/loopcsvrecorder.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc<2 || argc>5) {std::fprintf(stderr,"Usage: loop_csv_dense_realtime_test OUTPUT_DIRECTORY [SECONDS [ENCODERS [PATTERN]]]\n");return 2;}
    const QString root=QString::fromLocal8Bit(argv[1]);
    if(!QDir().mkpath(root))return 2;
    pv::data::LoopCsvRecorder r; QString error; bool failed=false;
    bool secondsOk=true,threadsOk=true,spacingOk=true;
    const unsigned seconds=argc>2?QString::fromLocal8Bit(argv[2]).toUInt(&secondsOk):15;
    const unsigned threads=argc>3?QString::fromLocal8Bit(argv[3]).toUInt(&threadsOk):0;
    const unsigned spacing=argc>4?QString::fromLocal8Bit(argv[4]).toUInt(&spacingOk):2;
    if(!secondsOk || !threadsOk || !spacingOk || !seconds || spacing<1 || spacing>3)return 2;
    if(!r.start(root,50000000,{"A","B","C","CLK","D","CS"},[&](const QString &s){failed=true;std::fprintf(stderr,"%s\n",qPrintable(s));},&error,
        pv::data::LoopCsvRecorder::DefaultSegmentBytes,pv::data::LoopCsvRecorder::DefaultBlockBytes,
        pv::data::LoopCsvRecorder::DefaultQueueBytes,3000000000ull,threads))return 2;
    const size_t groups=16384; std::vector<unsigned char> block(groups*48,0);
    for(size_t g=0;g<groups;g++)for(int b=0;b<8;b++)block[g*48+3*8+b]=spacing==1?0xaa:0x33;
    if(spacing==3) {
        uint64_t rng=12345;
        for(size_t g=0;g<groups;g++)for(int c=0;c<6;c++) {
            rng^=rng<<13; rng^=rng>>7; rng^=rng<<17;
            std::memcpy(block.data()+g*48+c*8,&rng,8);
        }
    }
    uint64_t sent=0; size_t peak=0; auto start=std::chrono::steady_clock::now(); unsigned report=5;
    while(sent<uint64_t(seconds)*50000000ull) {
        if(!r.submit(block.data(),block.size()))break;
        sent+=groups*64; peak=std::max(peak,r.queuedBytes());
        auto due=start+std::chrono::microseconds(sent/50);
        while(std::chrono::steady_clock::now()<due) QThread::msleep(1);
        if(sent/50000000>=report) {
            std::printf("seconds=%u queue_MiB=%.1f processed=%llu\n",report,r.queuedBytes()/1048576.,(unsigned long long)r.samplesWritten());
            std::fflush(stdout);report+=5;
        }
    }
    const auto stop=std::chrono::steady_clock::now();
    r.finish();r.wait();
    auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    const double drain=std::chrono::duration<double>(std::chrono::steady_clock::now()-stop).count();
    std::printf("threads=%u sent=%llu processed=%llu elapsed=%.2f drain=%.2f peak_queue_MiB=%.1f compressed_MiB=%.1f format_worker_s=%.2f compression_worker_s=%.2f failed=%d directory=%s\n",r.encoderThreads(),(unsigned long long)sent,(unsigned long long)r.samplesWritten(),elapsed,drain,peak/1048576.0,r.bytesWritten()/1048576.,r.formatMicros()/1e6,r.compressionMicros()/1e6,int(failed),qPrintable(r.outputDirectory()));
    return failed || sent!=r.samplesWritten() ? 1 : 0;
}
