// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QThread>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../DSView/pv/data/loopcsvrecorder.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if(argc!=3) {std::fprintf(stderr,"Usage: loop_csv_realtime_replay_test INPUT_DIRECTORY OUTPUT_DIRECTORY\n");return 2;}
    const QString root=QString::fromLocal8Bit(argv[1]),out=QString::fromLocal8Bit(argv[2]);
    if(!QDir().mkpath(out))return 2;
    std::array<QFile,6> files;
    for(int c=0;c<6;c++) { files[c].setFileName(root+QString("/ch%1.bin").arg(c)); if(!files[c].open(QIODevice::ReadOnly)) return 1; }
    const qint64 channelBytes=files[0].size();
    if(channelBytes<=0 || channelBytes%8)return 4;
    for(int c=1;c<6;c++)if(files[c].size()!=channelBytes)return 5;
    const uint64_t expectedSamples=uint64_t(channelBytes)*8*4;
    pv::data::LoopCsvRecorder recorder; QString error; bool failed=false;
    if(!recorder.start(out,50000000,{"IO-0","IO-1","IO-2","CLK","IO-3","CS-0"},
       [&](const QString &s){std::fprintf(stderr,"error: %s\n",qPrintable(s));failed=true;},&error)) return 2;
    const auto start=std::chrono::steady_clock::now();
    uint64_t sent=0; size_t peak=0; const int chunk=8*16384;
    for(int rep=0;rep<4;rep++) {
        for(auto &f:files) if(!f.seek(0))return 3;
        for(;;) {
            std::array<QByteArray,6> data;
            for(int c=0;c<6;c++) data[c]=files[c].read(chunk);
            if(data[0].isEmpty()) break;
            int len=data[0].size(); if(len%8) return 4;
            for(int c=1;c<6;c++) if(data[c].size()!=len) return 5;
            std::vector<unsigned char> cross(size_t(len)*6);
            for(int g=0;g<len/8;g++) for(int c=0;c<6;c++)
                std::memcpy(cross.data()+(size_t(g)*6+c)*8,data[c].constData()+g*8,8);
            if(!recorder.submit(cross.data(),cross.size())) { recorder.finish(); recorder.wait(); return 6; }
            sent+=uint64_t(len)*8; peak=std::max(peak,recorder.queuedBytes());
            const auto due=start+std::chrono::microseconds(sent/50);
            while(std::chrono::steady_clock::now()<due) QThread::msleep(1);
        }
    }
    const auto sentAt=std::chrono::steady_clock::now();
    recorder.finish(); recorder.wait();
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    const double drain=std::chrono::duration<double>(std::chrono::steady_clock::now()-sentAt).count();
    std::printf("directory=%s sent=%llu processed=%llu seconds=%.2f drain=%.2f peak_queue_MiB=%.1f compressed_MiB=%.1f segments=%u failed=%d\n",
       qPrintable(recorder.outputDirectory()),(unsigned long long)sent,(unsigned long long)recorder.samplesWritten(),seconds,drain,
       peak/1048576.0,recorder.bytesWritten()/1048576.0,recorder.segmentCount(),int(failed));
    return failed || sent!=recorder.samplesWritten() || sent!=expectedSamples ? 7 : 0;
}
