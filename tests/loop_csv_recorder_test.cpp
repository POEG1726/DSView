// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include "compressed_csv_test_utils.h"
#include <QTextStream>
#include <cstdio>
#include <atomic>
#include <thread>
#include <cstring>
#include <utility>
#include <vector>
#include "../DSView/pv/data/loopcsvrecorder.h"
using Edge=std::pair<quint64,quint64>;
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir tmp; if(!tmp.isValid()) return 1;
    const int channels=3, groups=1024;std::vector<unsigned char> raw(groups*channels*8);std::vector<Edge> expected;
    quint64 previous=0;for(int i=0;i<groups*64;i++) { quint64 value=((i/17)%2)|(((i/193)%2)<<1)|(((i/997)%2)<<2);
        if(i==0||value!=previous) expected.emplace_back(i,value);previous=value;
        for(int c=0;c<channels;c++) if(value & (1ull<<c)) raw[(i/64)*channels*8+c*8+(i%64)/8]|=1<<(i%8);
    }
    pv::data::LoopCsvRecorder r;QString error;bool failed=false;
    if(!r.start(tmp.path(),50000000,{"A","B","C"},[&](const QString &e){std::fprintf(stderr,"error %s\n",qPrintable(e));failed=true;},&error,1024,149,100000)) {std::fprintf(stderr,"start %s\n",qPrintable(error));return 2;}
    size_t off=0;while(off<raw.size()){size_t n=std::min<size_t>((off%211)+1,raw.size()-off);if(!r.submit(raw.data()+off,n))return 3;off+=n;}r.finish();r.wait();if(failed||r.samplesWritten()!=groups*64)return 4;
    QDir dir(r.outputDirectory());auto files=dir.entryList({"*.csv.zst"},QDir::Files,QDir::Name);if(files.size()<2)return 5;
    std::vector<Edge> actual;quint64 lastEnd=0;int segments=0;
    for(const QString &name:files) {QByteArray decoded=readCompressedCsv(dir.filePath(name));if(decoded.isEmpty()||decoded.size()>1024)return 6;QBuffer f(&decoded);f.open(QIODevice::ReadOnly);QTextStream in(&f);quint64 start=~quint64(0),end=~quint64(0);bool first=true;
        while(!in.atEnd()){QString line=in.readLine();if(line.startsWith("; Start sample (inclusive): "))start=line.mid(28).toULongLong();else if(line.startsWith("; End sample (exclusive): "))end=line.mid(26).toULongLong();else if(!line.startsWith(';')&&!line.startsWith("Sample,")){auto p=line.split(',');if(p.size()!=5)return 7;quint64 idx=p[0].toULongLong();quint64 v=p[2].toInt()|(p[3].toInt()<<1)|(p[4].toInt()<<2);if(first && idx!=start)return 8;const bool repeatedState=first && !actual.empty() && actual.back().second==v;first=false;if(!repeatedState)actual.emplace_back(idx,v);}}
        if(start!=lastEnd||end<start||end==~quint64(0))return 9;lastEnd=end;++segments;
    }
    if(actual!=expected||lastEnd!=groups*64)return 10;
    // A completed raw block is visible in the active .part before capture stops.
    pv::data::LoopCsvRecorder live; bool liveError=false;
    if(!live.start(tmp.path(),1000,{"A"},[&](const QString &){liveError=true;},&error,1024*1024,8,1024))return 19;
    unsigned char liveBlock[8]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa};
    if(!live.submit(liveBlock,sizeof(liveBlock)))return 20;
    QElapsedTimer waitForWrite;waitForWrite.start();
    while(live.samplesWritten()<64 && waitForWrite.elapsed()<5000)QThread::msleep(1);
    if(live.samplesWritten()!=64)return 21;
    if(!readCompressedCsv(QDir(live.outputDirectory()).filePath("capture_000001.csv.zst.part")).contains("Sample,Time(s)"))return 22;
    live.finish();live.wait();if(liveError)return 23;
    // Idle and changing signals must still split exactly at each sample-count boundary.
    pv::data::LoopCsvRecorder timed; bool timedError=false;
    if(!timed.start(tmp.path(),1,{"A"},[&](const QString &){timedError=true;},&error,1024*1024,8,1024,60))return 24;
    unsigned char idle[8]={0}; if(!timed.submit(idle,8) || !timed.submit(idle,8))return 25;
    timed.finish();timed.wait(); if(timedError || timed.samplesWritten()!=128)return 26;
    QDir td(timed.outputDirectory()); auto tf=td.entryList({"*.csv.zst"},QDir::Files,QDir::Name);
    if(tf.size()!=3)return 27;
    for(int i=0;i<3;i++) { QByteArray t=readCompressedCsv(td.filePath(tf[i]));
       if(!t.contains("; Start sample (inclusive): "+QByteArray::number(i*60)+"\n") ||
          !t.contains("; End sample (exclusive): "+QByteArray::number(i==2?128:(i+1)*60)+"\n") ||
          !t.contains("\n"+QByteArray::number(i*60)+","))return 28;
    }
    pv::data::LoopCsvRecorder partial;bool partialError=false;
    if(!partial.start(tmp.path(),1,{"A","B","C"},[&](const QString &){partialError=true;},&error,1024,64,1024))return 11;
    unsigned char byte=1;if(!partial.submit(&byte,1))return 12;partial.finish();partial.wait();
    QDir partialDir(partial.outputDirectory());if(!partialError||partialDir.entryList({"*.part"},QDir::Files).isEmpty())return 13;
    pv::data::LoopCsvRecorder pressure; bool queueError=false;
    if(!pressure.start(tmp.path(),1000,{"A"},[&](const QString &){queueError=true;},&error,1024*1024,64,64))return 14;
    unsigned char alternating[64];std::memset(alternating,0xaa,sizeof(alternating));bool rejected=false; unsigned acceptedBlocks=0;
    pressure.markCaptureStopRequested(); // A stop request must not mask overflow.
    for(int i=0;i<100000 && !rejected;i++) { rejected=!pressure.submit(alternating,sizeof(alternating)); if(!rejected) ++acceptedBlocks; }
    pressure.finish();pressure.wait();QDir pressureDir(pressure.outputDirectory());
    auto parts=pressureDir.entryList({"*.part"},QDir::Files);
    if(!rejected || !queueError || !pressure.failed() || parts.isEmpty()) return 15;
    if(!readCompressedCsv(pressureDir.filePath(parts.last())).contains("; End sample (exclusive): "))return 29;
    if(pressure.samplesWritten()!=uint64_t(acceptedBlocks+1)*512)return 30;
    pv::data::LoopCsvRecorder invalid;
    if(invalid.start(tmp.path()+"/missing",1000,{"A"},[](const QString &){},&error))return 16;
    pv::data::LoopCsvRecorder stopping;std::atomic<unsigned> accepted(0);
    if(!stopping.start(tmp.path(),1000,{"A"},[](const QString &){},&error,1024*1024,1024,4*1024*1024))return 17;
    std::thread producer([&](){unsigned char zero[8]={0};for(unsigned i=0;i<100000;i++){if(!stopping.submit(zero,sizeof(zero)))break;++accepted;}});
    while(accepted.load()<100)std::this_thread::yield();stopping.finish();producer.join();stopping.wait();
    if(stopping.failed() || stopping.samplesWritten()!=uint64_t(accepted.load())*64)return 18;
    std::printf("Loop CSV: %d rotated files, %llu samples, %zu edges; incomplete tail and queue overflow retained as .part\n",segments,(unsigned long long)lastEnd,actual.size());return 0;
}
