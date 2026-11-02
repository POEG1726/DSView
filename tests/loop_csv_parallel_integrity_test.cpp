// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <utility>
#include <vector>
#include "../DSView/pv/data/loopcsvrecorder.h"
#include "compressed_csv_test_utils.h"

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir temp; if(!temp.isValid())return 1;
    const uint64_t samples=1048576, channels=6, bytesPerGroup=48;
    std::vector<uint8_t> raw(size_t(samples/64*bytesPerGroup),0);
    std::vector<std::pair<uint64_t,uint64_t>> expected;
    uint64_t rng=12345, previous=~uint64_t(0);
    for(uint64_t i=0;i<samples;i++) {
        rng^=rng<<13; rng^=rng>>7; rng^=rng<<17;
        // Include long idle regions crossing both jobs and sample boundaries.
        const uint64_t value=(i>=90000 && i<160000)?17:rng&63;
        if(value!=previous)expected.emplace_back(i,value); previous=value;
        for(uint64_t c=0;c<channels;c++) if((value>>c)&1)
            raw[size_t(i/64*bytesPerGroup+c*8+(i%64)/8)] |= uint8_t(1u<<(i%8));
    }
    for(unsigned threads:{1u,8u}) {
        pv::data::LoopCsvRecorder recorder; QString error; bool failed=false;
        if(!recorder.start(temp.path(),50000000,{"A","B","C","D","E","F"},[&](const QString &s){failed=true;std::fprintf(stderr,"%s\n",qPrintable(s));},
            &error,128*1024,512*1024,16*1024*1024,100003,threads))return 2;
        for(size_t offset=0;offset<raw.size();) {
            const size_t amount=std::min<size_t>(65537+(offset%257),raw.size()-offset);
            if(!recorder.submit(raw.data()+offset,amount))return 3;
            offset+=amount;
        }
        recorder.finish();recorder.wait();if(failed || recorder.samplesWritten()!=samples)return 4;
        QDir directory(recorder.outputDirectory()); const auto names=directory.entryList({"*.csv.zst"},QDir::Files,QDir::Name);
        uint64_t end=0,lastValue=~uint64_t(0); size_t next=0;
        for(const QString &name:names) {
            bool complete=false; const QByteArray decoded=readCompressedCsv(directory.filePath(name),&complete);
            if(!complete || decoded.size()>128*1024)return 5;
            const auto lines=decoded.split('\n');uint64_t start=~uint64_t(0),segmentEnd=~uint64_t(0);bool first=true;
            for(const auto &line:lines) {
                if(line.startsWith("; Start sample (inclusive): "))start=line.mid(28).toULongLong();
                else if(line.startsWith("; End sample (exclusive): "))segmentEnd=line.mid(26).toULongLong();
                else if(!line.isEmpty() && !line.startsWith(';') && !line.startsWith("Sample,")) {
                    const auto fields=line.split(',');if(fields.size()!=8)return 6;
                    const uint64_t index=fields[0].toULongLong(); uint64_t value=0;
                    for(int c=0;c<6;c++)value|=uint64_t(fields[c+2].toUInt())<<c;
                    if(first && index!=start)return 7;
                    const bool redundant=first && lastValue==value;
                    first=false;
                    if(!redundant) {
                        if(next>=expected.size() || expected[next]!=std::make_pair(index,value))return 8;
                        ++next;
                    }
                    if(std::abs(fields[1].toDouble()-index/50000000.)>0.0000000001)return 9;
                    lastValue=value;
                }
            }
            if(start!=end || segmentEnd<start || segmentEnd-start>100003)return 10;
            end=segmentEnd;
        }
        if(end!=samples || next!=expected.size())return 11;
        // Corrupt a compressed frame: checksum validation must reject the file.
        QFile corrupt(directory.filePath(names.first())); if(!corrupt.open(QIODevice::ReadWrite))return 12;
        if(!corrupt.seek(corrupt.size()-5))return 13;
        QByteArray byte=corrupt.read(1);byte[0]=char(byte[0]^1);
        corrupt.seek(corrupt.size()-5);corrupt.write(byte);corrupt.close();
        bool complete=true;readCompressedCsv(corrupt.fileName(),&complete);if(complete)return 14;
        std::printf("threads=%u samples=%llu transitions=%zu files=%d: exact reconstruction, timestamps and corruption detection passed\n",
            threads,(unsigned long long)samples,next,names.size());
    }
    return 0;
}
