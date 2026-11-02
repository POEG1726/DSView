// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore>
#include <cstdio>
#include <vector>
#include "../DSView/pv/data/loopcsvrecorder.h"
#include "compressed_csv_test_utils.h"

static uint64_t valueAt(uint64_t sample,unsigned channels) {
    uint64_t value=0;
    for(unsigned c=0;c<channels;c++)value|=((sample/(c+1))&1)<<c;
    return value;
}
static bool checkStop(const QString &root,unsigned channels,size_t tail) {
    const size_t group=channels*8;
    std::vector<uint8_t> raw(3*group,0);
    for(uint64_t i=0;i<192;i++)for(unsigned c=0;c<channels;c++)
        if((valueAt(i,channels)>>c)&1)raw[size_t(i/64)*group+c*8+size_t(i%64)/8]|=uint8_t(1u<<(i%8));
    QStringList names;for(unsigned c=0;c<channels;c++)names<<QString("CH-%1").arg(c);
    pv::data::LoopCsvRecorder recorder;QString error;bool failed=false;
    if(!recorder.start(root,50000000,names,[&](const QString &e){failed=true;std::fprintf(stderr,"%s\n",qPrintable(e));},
        &error,2048,29,64*1024,131,2))return false;
    const size_t bytes=2*group+tail;
    size_t offset=0;
    while(offset<bytes) {
        const size_t length=std::min<size_t>(offset%17+1,bytes-offset);
        if(!recorder.submit(raw.data()+offset,length))return false;
        offset+=length;
    }
    recorder.markCaptureStopRequested();recorder.finish();recorder.wait();
    const size_t shared=tail>(channels-1)*8?tail-(channels-1)*8:0;
    const uint64_t total=128+shared*8;
    if(failed || recorder.failed() || recorder.samplesWritten()!=total || recorder.finalTailBytes()!=tail ||
       recorder.finalTailSamples()!=shared*8 || recorder.omittedTailBytes()!=tail-shared*channels)return false;
    QDir directory(recorder.outputDirectory());if(!directory.entryList({"*.part"},QDir::Files).isEmpty())return false;
    const auto files=directory.entryList({"*.csv.zst"},QDir::Files,QDir::Name);
    uint64_t next=0,previousEnd=0;
    for(const QString &name:files) {
        bool complete=false;const QByteArray decoded=readCompressedCsv(directory.filePath(name),&complete);
        if(!complete || decoded.size()>2048)return false;
        uint64_t start=~uint64_t(0),end=~uint64_t(0);
        for(const auto &line:decoded.split('\n')) {
            if(line.startsWith("; Start sample (inclusive): "))start=line.mid(28).toULongLong();
            else if(line.startsWith("; End sample (exclusive): "))end=line.mid(26).toULongLong();
            else if(!line.isEmpty() && line[0]>='0' && line[0]<='9') {
                const auto fields=line.split(',');if(fields.size()!=int(channels+2))return false;
                const uint64_t index=fields[0].toULongLong();uint64_t value=0;
                for(unsigned c=0;c<channels;c++)value|=uint64_t(fields[int(c)+2].toUInt())<<c;
                if(index!=next || value!=valueAt(index,channels))return false;
                ++next;
            }
        }
        if(start!=previousEnd || end<start || end-start>131)return false;previousEnd=end;
    }
    return next==total && previousEnd==total;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir temp;if(!temp.isValid())return 1;
    unsigned cases=0;
    for(unsigned channels:{1u,6u,16u})for(size_t tail=0;tail<channels*8;tail++) {
        if(!checkStop(temp.path(),channels,tail)) {
            std::fprintf(stderr,"Requested-stop tail failed: channels=%u bytes=%zu\n",channels,tail);return 2;
        }
        ++cases;
    }
    // An explicit stop must not hide a real error already received.
    pv::data::LoopCsvRecorder failed;QString error;bool notified=false;
    if(!failed.start(temp.path(),50000000,{"A"},[&](const QString &){notified=true;},&error))return 3;
    unsigned char data=0xaa;if(!failed.submit(&data,1))return 4;
    failed.markCaptureStopRequested();failed.fail("Simulated USB error");failed.finish();failed.wait();
    QDir bad(failed.outputDirectory());
    if(!notified || !failed.failed() || bad.entryList({"*.part"},QDir::Files).isEmpty() || !bad.entryList({"*.csv.zst"},QDir::Files).isEmpty())return 5;
    std::printf("%u requested-stop tails: exact valid samples, bounds, no padded samples, metadata and error preservation passed\n",cases);
    return 0;
}
