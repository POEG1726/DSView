// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QThread>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../DSView/pv/data/loopcsvrecorder.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=3) {std::fprintf(stderr,"Usage: loop_csv_capture_file_test INPUT_DIRECTORY OUTPUT_DIRECTORY\n");return 2;}
    const QString root=QString::fromLocal8Bit(argv[1]),out=QString::fromLocal8Bit(argv[2]);
    std::array<QFile,6> files;for(int c=0;c<6;c++){files[c].setFileName(root+QString("/ch%1.bin").arg(c));if(!files[c].open(QIODevice::ReadOnly))return 1;}
    const qint64 channelBytes=files[0].size();
    if(channelBytes<=0 || channelBytes%8)return 3;
    for(int c=1;c<6;c++)if(files[c].size()!=channelBytes)return 4;
    const uint64_t expectedSamples=uint64_t(channelBytes)*8;
    if(!QDir().mkpath(out))return 2;
    pv::data::LoopCsvRecorder recorder;QString error;bool failed=false;
    if(!recorder.start(out,50000000,{"IO-0","IO-1","IO-2","CLK","IO-3","CS-0"},[&](const QString &s){std::fprintf(stderr,"error %s\n",qPrintable(s));failed=true;},&error))return 2;
    const int chunk=8*32768;for(;;){std::array<QByteArray,6> data;for(int c=0;c<6;c++) data[c]=files[c].read(chunk);if(data[0].isEmpty())break;
        const int len=data[0].size();if(len%8)return 3;for(int c=1;c<6;c++)if(data[c].size()!=len)return 4;
        std::vector<unsigned char> cross(size_t(len)*6);for(int g=0;g<len/8;g++)for(int c=0;c<6;c++)std::memcpy(cross.data()+(size_t(g)*6+c)*8,data[c].constData()+g*8,8);
        while(recorder.queuedBytes()>64u*1024*1024 && recorder.running())QThread::msleep(1);
        if(!recorder.submit(cross.data(),cross.size()))return 5;
    }
    recorder.finish();recorder.wait();if(failed)return 6;
    std::printf("directory=%s samples=%llu segments=%u bytes=%llu\n",qPrintable(recorder.outputDirectory()),(unsigned long long)recorder.samplesWritten(),recorder.segmentCount(),(unsigned long long)recorder.bytesWritten());
    return recorder.samplesWritten()==expectedSamples?0:7;
}
