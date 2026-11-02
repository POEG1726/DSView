// SPDX-License-Identifier: GPL-3.0-or-later
#include "loopcsvrecorder.h"
#include "logiccsvedges.h"
#include <QDateTime>
#include <QDir>
#include <QIODevice>
#include <zstd.h>
#include <future>
#include <queue>
#include <utility>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace pv { namespace data {
namespace {
using Clock = std::chrono::steady_clock;
uint64_t micros(Clock::duration duration) {
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
}
QByteArray compressFrame(const QByteArray &text) {
    struct Context {
        ZSTD_CCtx *ptr=ZSTD_createCCtx();
        Context() {
            if(!ptr) throw std::bad_alloc();
            ZSTD_CCtx_setParameter(ptr,ZSTD_c_compressionLevel,1);
            ZSTD_CCtx_setParameter(ptr,ZSTD_c_checksumFlag,1);
        }
        ~Context() { ZSTD_freeCCtx(ptr); }
    };
    thread_local Context context;
    QByteArray compressed(int(ZSTD_compressBound(size_t(text.size()))),Qt::Uninitialized);
    const size_t size=ZSTD_compress2(context.ptr,compressed.data(),size_t(compressed.size()),text.constData(),size_t(text.size()));
    if(ZSTD_isError(size)) throw std::runtime_error(ZSTD_getErrorName(size));
    compressed.resize(int(size)); return compressed;
}
char *appendInteger(char *out, uint64_t n) {
    static const char pairs[]="00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
    char digits[24]; char *p=digits+sizeof(digits);
    while(n>=100) { const unsigned pair=unsigned(n%100); n/=100; p-=2; std::memcpy(p,pairs+pair*2,2); }
    if(n>=10) { p-=2; std::memcpy(p,pairs+n*2,2); } else *--p=char('0'+n);
    const size_t length=size_t(digits+sizeof(digits)-p); std::memcpy(out,p,length); return out+length;
}
// Only this bounded pool owns encoder tasks. Files are exclusively owned by run().
class EncoderPool {
    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> threads_;
    bool stop_=false;
public:
    explicit EncoderPool(unsigned count) {
        try {
            for(unsigned i=0;i<count;i++) threads_.emplace_back([this] {
                for(;;) {
                    std::function<void()> task;
                    { std::unique_lock<std::mutex> lock(mutex_);
                      condition_.wait(lock,[this]{return stop_ || !tasks_.empty();});
                      if(stop_) return;
                      task=std::move(tasks_.front()); tasks_.pop(); }
                    task();
                }
            });
        } catch(...) { shutdown(); throw; }
    }
    void shutdown() {
        { std::lock_guard<std::mutex> lock(mutex_); stop_=true;
          while(!tasks_.empty()) tasks_.pop(); }
        condition_.notify_all();
        for(auto &thread:threads_) if(thread.joinable()) thread.join();
    }
    ~EncoderPool() { shutdown(); }
    template<class Function>
    auto submit(Function function) -> std::future<typename std::result_of<Function()>::type> {
        using Result=typename std::result_of<Function()>::type;
        auto task=std::make_shared<std::packaged_task<Result()>>(std::move(function));
        auto result=task->get_future();
        { std::lock_guard<std::mutex> lock(mutex_); tasks_.emplace([task]{(*task)();}); }
        condition_.notify_one(); return result;
    }
};
QByteArray csvName(const QString &name) {
    QByteArray out = name.toUtf8();
    out.replace("\"", "\"\"");
    return '"' + out + '"';
}
}
LoopCsvRecorder::LoopCsvRecorder() = default;
LoopCsvRecorder::~LoopCsvRecorder() { finish(); wait(); }

bool LoopCsvRecorder::start(const QString &directory, uint64_t rate, const QStringList &channels,
        ErrorHandler handler, QString *error, uint64_t segmentBytes, size_t blockBytes, size_t queueBytes, uint64_t segmentSamples, unsigned encoderThreads) {
    if (running_) { if(error) *error="A Loop CSV recording is still finishing."; return false; }
    wait();
    if ((!rate || rate > std::numeric_limits<uint64_t>::max() / 60) || channels.isEmpty() || channels.size() > 64 || segmentBytes < 1024 || !blockBytes || queueBytes < blockBytes || !segmentSamples) {
        if(error) *error="Invalid Loop CSV recording settings.";
        return false;
    }
    QDir root(directory);
    if (!root.exists()) { if(error) *error="The recording directory does not exist."; return false; }
    const QString base = "DSView-loop-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    QString name=base; unsigned suffix=0;
    while (!root.mkdir(name)) {
        if (!root.exists(name) || ++suffix > 9999) { if(error) *error="Cannot create the recording directory."; return false; }
        name=base+QString("-%1").arg(suffix);
    }
    runDir_=root.filePath(name); rate_=rate; channels_=channels; segmentLimit_=segmentBytes; segmentSamples_=segmentSamples;
    blockLimit_=blockBytes; queueLimit_=queueBytes; errorHandler_=handler;
    startedUtc_=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    finish_=false; ready_=false; error_.clear(); failed_=false; drainOnFailure_=false; samples_=0; bytes_=0; elapsedMicros_=0; queuedBytes_=0; segments_=0;
    stopRequested_=false; tailBytes_=0; tailSamples_=0; omittedTailBytes_=0;
    queue_.clear(); producer_.clear(); formatMicros_=0; compressionMicros_=0;
    fractionDigits_=0; fractionScale_=0;
    uint64_t power=1;
    for(unsigned digits=0;digits<=9;digits++,power*=10) {
        if(power%rate_==0) { fractionDigits_=digits; fractionScale_=power/rate_; break; }
    }
    channelSuffixes_.clear();
    if(channels_.size()<=12) {
        channelSuffixes_.resize(size_t(1)<<channels_.size());
        for(size_t value=0;value<channelSuffixes_.size();value++) {
            QByteArray &suffix=channelSuffixes_[value];
            for(int c=0;c<channels_.size();c++) { suffix+=','; suffix+=((value>>c)&1)?'1':'0'; }
            suffix+='\n';
        }
    }
    const unsigned hardware=std::max(1u,std::thread::hardware_concurrency());
    encoderThreads_=encoderThreads ? std::min(encoderThreads,32u) : std::min(12u,hardware>2?hardware-2:1u);
    try { producer_.reserve(blockLimit_); }
    catch (const std::exception &ex) { if(error) *error=QString::fromLocal8Bit(ex.what()); return false; }
    segmentBytes_=0; segmentStart_=0; segmentHasRow_=false; running_=true;
    try { worker_=std::thread(&LoopCsvRecorder::run,this); }
    catch(const std::exception &ex) { running_=false; if(error) *error=QString::fromLocal8Bit(ex.what()); return false; }
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock,[this]{return ready_;});
    if(failed_) { if(error) *error=error_; lock.unlock(); wait(); return false; }
    accepting_=true;
    return true;
}

void LoopCsvRecorder::setError(const QString &error) {
    bool expected=false;
    if (!failed_.compare_exchange_strong(expected,true)) return;
    { std::lock_guard<std::mutex> lock(mutex_); error_=error; }
    condition_.notify_all();
    if(errorHandler_) errorHandler_(error);
}
void LoopCsvRecorder::fail(const QString &error) { setError(error); }

bool LoopCsvRecorder::queueBlock(std::vector<uint8_t> &&block, bool finalBlock) {
    if(block.empty()) return true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(finish_ || failed_) return false;
        if(!finalBlock && block.size() > queueLimit_ - std::min(queueLimit_,queuedBytes_.load())) return false;
        queuedBytes_ += block.size(); queue_.push_back(std::move(block));
    }
    condition_.notify_one(); return true;
}

bool LoopCsvRecorder::submit(const void *bytes, size_t size) {
    std::lock_guard<std::mutex> producerLock(producerMutex_);
    if(!running_ || failed_ || !accepting_) return false;
    const uint8_t *data=static_cast<const uint8_t *>(bytes);
    try {
        while(size) {
            size_t n=std::min(size,blockLimit_-producer_.size());
            producer_.insert(producer_.end(),data,data+n); data+=n;size-=n;
            if(producer_.size()==blockLimit_) {
                if(!queueBlock(std::move(producer_))) {
                    // Keep the rejected full block and drain the existing queue. The
                    // remainder of this incoming packet cannot be accepted safely.
                    if (!failed_ && queueBlock(std::move(producer_), true)) drainOnFailure_=true;
                    setError("Loop CSV write queue is full; capture stopped and buffered data is being saved. The recording is incomplete.");
                    return false;
                }
                producer_=std::vector<uint8_t>();producer_.reserve(blockLimit_);
            }
        }
    } catch(const std::exception &e) { setError(QString("Loop CSV buffer error: %1").arg(QString::fromLocal8Bit(e.what())));return false; }
    return true;
}

void LoopCsvRecorder::finish() {
    std::lock_guard<std::mutex> producerLock(producerMutex_);
    if(!running_ || !accepting_) return;
    accepting_=false;
    try {
        if(!failed_ && !producer_.empty() && !queueBlock(std::move(producer_), true))
            setError("Loop CSV queue is full while finalizing; the final segment is incomplete.");
    } catch(const std::exception &ex) {
        setError(QString("Loop CSV final buffer error: %1").arg(QString::fromLocal8Bit(ex.what())));
    }
    producer_.clear();
    { std::lock_guard<std::mutex> lock(mutex_); finish_=true; }
    condition_.notify_all();
}
void LoopCsvRecorder::wait() { if(worker_.joinable()) worker_.join(); }

bool LoopCsvRecorder::openSegment(uint64_t startIndex, QString *error) {
    const unsigned number=segments_.load()+1;
    const QString name=QString("capture_%1.csv.zst").arg(number,6,10,QChar('0'));
    finalPath_=QDir(runDir_).filePath(name); partPath_=finalPath_+".part";
    file_->setFileName(partPath_);
    if(!file_->open(QIODevice::WriteOnly | QIODevice::NewOnly)) { *error=file_->errorString(); return false; }
    segmentBytes_=0; segmentStart_=startIndex; segmentHasRow_=false;
    QByteArray header="; DSView Loop CSV, compressed changes\n; Recorder initialized UTC: "+startedUtc_.toUtf8()+"\n; Sample rate (Hz): "+QByteArray::number(rate_)+"\n; Start sample (inclusive): "+QByteArray::number(startIndex)+"\nSample,Time(s)";
    for(const QString &ch:channels_) header+=','+csvName(ch);
    header+='\n';
    if(uint64_t(header.size())+256>=segmentLimit_) { *error="CSV segment limit is too small for the header."; file_->close(); return false; }
    if(!writeText(header,error)) return false;
    segmentBytes_=header.size(); segments_=number; return true;
}
bool LoopCsvRecorder::writeBytes(const QByteArray &data, QString *error) {
    qint64 offset=0;
    while(offset<data.size()) {
        const qint64 n=file_->write(data.constData()+offset,data.size()-offset);
        if(n<=0) { *error=file_->errorString(); return false; }
        offset+=n; bytes_+=uint64_t(n);
    }
    return true;
}
bool LoopCsvRecorder::writeText(const QByteArray &text, QString *error) {
    return writeBytes(compressFrame(text),error);
}
bool LoopCsvRecorder::closeSegment(uint64_t endIndex, QString *error) {
    QByteArray tail;
    if(stopRequested_)tail+="; Capture stop requested\n";
    if(tailBytes_) {
        tail+="; Unaligned input tail bytes: "+QByteArray::number(qulonglong(tailBytes_.load()))+'\n';
        tail+="; Tail samples recovered: "+QByteArray::number(qulonglong(tailSamples_.load()))+'\n';
        tail+="; Unpaired channel bytes omitted: "+QByteArray::number(qulonglong(omittedTailBytes_.load()))+'\n';
    }
    tail+="; End sample (exclusive): "+QByteArray::number(endIndex)+"\n";
    if(!writeText(tail,error) || !file_->flush()) { if(error->isEmpty())*error=file_->errorString(); return false; }
    file_->close();
    if(file_->error()!=QFileDevice::NoError) { *error=file_->errorString(); return false; }
    if(!file_->rename(finalPath_)) { *error=file_->errorString(); return false; }
    return true;
}
int LoopCsvRecorder::formatRow(char *buffer, uint64_t index, uint64_t value) const {
    char *out=appendInteger(buffer,index); *out++=',';
    if(fractionScale_) {
        out=appendInteger(out,index/rate_);
        uint64_t fraction=(index%rate_)*fractionScale_;
        if(fraction) {
            *out++='.'; char *last=out+fractionDigits_;
            for(char *p=last;p!=out;) { *--p=char('0'+fraction%10); fraction/=10; }
            while(last>out && last[-1]=='0') --last;
            out=last;
        }
    } else {
        const QByteArray time=QByteArray::number(double(index)/double(rate_),'g',17);
        std::memcpy(out,time.constData(),size_t(time.size())); out+=time.size();
    }
    if(!channelSuffixes_.empty()) {
        const QByteArray &suffix=channelSuffixes_[size_t(value)];
        std::memcpy(out,suffix.constData(),size_t(suffix.size())); out+=suffix.size();
    } else {
        for(int c=0;c<channels_.size();c++) { *out++=','; *out++=((value>>c)&1)?'1':'0'; }
        *out++='\n';
    }
    return int(out-buffer);
}
QByteArray LoopCsvRecorder::row(uint64_t index, uint64_t value) const {
    char buffer[256]; const int size=formatRow(buffer,index,value); return QByteArray(buffer,size);
}
LoopCsvRecorder::EncodedChunk LoopCsvRecorder::encodeChunk(
        const std::shared_ptr<std::vector<uint8_t>> &raw, uint64_t base,
        uint64_t previous, size_t pageLimit, uint64_t validSamples) const {
    const auto started=Clock::now();
    EncodedChunk result;
    const size_t channels=size_t(channels_.size()), groupBytes=channels*8;
    const uint64_t count=validSamples;
    if(!count || count>uint64_t(raw->size()/groupBytes)*64)
        throw std::runtime_error("Invalid final sample count.");
    std::vector<std::vector<uint8_t>> data(channels,std::vector<uint8_t>(raw->size()/groupBytes*8));
    std::vector<uint8_t *> ptrs; std::vector<bool> flags(channels,false);
    for(size_t c=0;c<channels;c++) ptrs.push_back(data[c].data());
    for(size_t g=0;g<raw->size()/groupBytes;g++) for(size_t c=0;c<channels;c++)
        std::memcpy(ptrs[c]+g*8,raw->data()+g*groupBytes+c*8,8);
    auto valueAt=[&](uint64_t at) {
        uint64_t value=0;
        for(size_t c=0;c<channels;c++) value|=uint64_t((ptrs[c][at/8]>>(at%8))&1)<<c;
        return value;
    };
    CsvEdgeScanner scanner; scanner.previous=previous; scanner.initialized=(base!=0);
    QByteArray text; text.reserve(int(pageLimit)); EncodedPage page;
    auto begin=[&](uint64_t at, uint64_t value, bool changed) {
        page=EncodedPage{}; page.first=base+at; page.firstRow=row(base+at,value); page.firstChanged=changed;
    };
    auto finishPage=[&](uint64_t end) {
        page.end=base+end; page.textBytes=uint64_t(text.size());
        if(!text.isEmpty()) {
            const auto compressionStart=Clock::now(); page.compressed=compressFrame(text);
            result.compressionMicros+=micros(Clock::now()-compressionStart);
        }
        text.clear(); result.pages.push_back(std::move(page));
    };
    for(uint64_t at=0;at<count;) {
        const uint64_t value=valueAt(at);
        begin(at,value,!scanner.initialized || scanner.previous!=value);
        const uint64_t span=std::min(count-at,segmentSamples_-(base+at)%segmentSamples_);
        scanner.scan(ptrs,flags,at,span,[&](uint64_t edge,uint64_t bits) {
            if(base+edge==page.first) return;
            char line[256]; const int length=formatRow(line,base+edge,bits);
            if(uint64_t(text.size()+length+page.firstRow.size())>pageLimit) {
                finishPage(edge); begin(edge,bits,true);
            } else text.append(line,length);
        });
        at+=span; finishPage(at);
    }
    result.formatMicros=micros(Clock::now()-started)-result.compressionMicros;
    return result;
}
bool LoopCsvRecorder::writePage(const EncodedPage &page, QString *error) {
    const uint64_t pageBytes=page.textBytes+uint64_t(page.firstRow.size());
    const bool sampleBoundary=page.first!=segmentStart_ && page.first%segmentSamples_==0;
    if(segmentHasRow_ && (sampleBoundary || segmentBytes_+pageBytes+FooterReserveBytes>segmentLimit_)) {
        if(!closeSegment(page.first,error) || !openSegment(page.first,error)) return false;
    }
    if(segmentBytes_+pageBytes+FooterReserveBytes>segmentLimit_) { *error="Encoded CSV page exceeds segment limit."; return false; }
    if(!segmentHasRow_ || page.firstChanged) {
        if(!writeText(page.firstRow,error)) return false;
        segmentBytes_+=uint64_t(page.firstRow.size()); segmentHasRow_=true;
    }
    if(!writeBytes(page.compressed,error)) return false;
    segmentBytes_+=page.textBytes;
    return true;
}
void LoopCsvRecorder::run() {
    QFile workerFile; file_=&workerFile; QString problem;
    try {
        if(!openSegment(0,&problem)) throw std::runtime_error(problem.toStdString());
    } catch(const std::exception &ex) {
        { std::lock_guard<std::mutex> lock(mutex_); error_=QString::fromLocal8Bit(ex.what()); failed_=true; ready_=true; }
        condition_.notify_all(); running_=false; return;
    }
    { std::lock_guard<std::mutex> lock(mutex_); ready_=true; }
    condition_.notify_all();
    const auto started=Clock::now();
    try {
        EncoderPool pool(encoderThreads_);
        std::deque<std::future<EncodedChunk>> pending;
        std::vector<uint8_t> remainder;
        uint64_t submitted=0, previous=0;
        const size_t channels=size_t(channels_.size()), groupBytes=channels*8;
        const size_t jobBytes=std::max(groupBytes,(256u*1024/groupBytes)*groupBytes);
        // Reserve worst-case sample-index growth and final metadata in every file.
        const uint64_t reserve=segmentBytes_+FooterReserveBytes+20;
        if(segmentLimit_<=reserve) throw std::runtime_error("CSV segment limit is too small.");
        const size_t pageLimit=size_t(std::min<uint64_t>(1024*1024,segmentLimit_-reserve));
        auto writeNext=[&] {
            EncodedChunk chunk=pending.front().get(); pending.pop_front();
            formatMicros_+=chunk.formatMicros; compressionMicros_+=chunk.compressionMicros;
            for(const auto &page:chunk.pages) {
                if(!writePage(page,&problem)) throw std::runtime_error(problem.toStdString());
                samples_=page.end;
            }
            if(!file_->flush()) throw std::runtime_error(file_->errorString().toStdString());
            elapsedMicros_=micros(Clock::now()-started);
        };
        for(;;) {
            std::vector<uint8_t> block;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if(queue_.empty() && !pending.empty()) { lock.unlock(); writeNext(); continue; }
                condition_.wait(lock,[this]{return failed_ || finish_ || !queue_.empty();});
                if(failed_ && (!drainOnFailure_ || queue_.empty())) break;
                if(queue_.empty()) { if(finish_) break; else continue; }
                block=std::move(queue_.front()); queue_.pop_front(); queuedBytes_-=block.size();
            }
            if(!remainder.empty()) { remainder.insert(remainder.end(),block.begin(),block.end()); block.swap(remainder); remainder.clear(); }
            const size_t used=block.size()/groupBytes*groupBytes;
            remainder.assign(block.begin()+used,block.end());
            for(size_t offset=0;offset<used;) {
                const size_t length=std::min(jobBytes,used-offset);
                auto raw=std::make_shared<std::vector<uint8_t>>(block.begin()+offset,block.begin()+offset+length);
                pending.push_back(pool.submit(std::bind(&LoopCsvRecorder::encodeChunk,this,raw,submitted,previous,pageLimit,uint64_t(length/groupBytes)*64)));
                previous=0;
                for(size_t c=0;c<channels;c++) previous|=uint64_t((raw->at(length-groupBytes+c*8+7)>>7)&1)<<c;
                submitted+=uint64_t(length/groupBytes)*64; offset+=length;
                if(pending.size()>=encoderThreads_*2) writeNext();
            }
        }
        while(!pending.empty() && (!failed_ || drainOnFailure_)) writeNext();
        if(!failed_ && !remainder.empty()) {
            if(!stopRequested_) {
                setError(QString("Loop CSV ended unexpectedly with %1 of %2 bytes in its final interleaved group.")
                    .arg(qulonglong(remainder.size())).arg(qulonglong(groupBytes)));
            } else {
                tailBytes_=remainder.size();
                const size_t lastChannelOffset=(channels-1)*8;
                const size_t sharedBytes=remainder.size()>lastChannelOffset ? remainder.size()-lastChannelOffset : 0;
                tailSamples_=sharedBytes*8;
                omittedTailBytes_=remainder.size()-sharedBytes*channels;
                if(sharedBytes) {
                    // Padding supplies storage only: the encoder is limited to the
                    // samples for which every channel has actually arrived.
                    auto raw=std::make_shared<std::vector<uint8_t>>(groupBytes,0);
                    std::copy(remainder.begin(),remainder.end(),raw->begin());
                    pending.push_back(pool.submit(std::bind(&LoopCsvRecorder::encodeChunk,this,raw,submitted,previous,pageLimit,uint64_t(sharedBytes)*8)));
                    writeNext();
                }
            }
        }
        if(!failed_) {
            if(!closeSegment(samples_,&problem)) setError("Loop CSV finalize failed: "+problem);
        } else if(drainOnFailure_) {
            if(!writeText("; End sample (exclusive): "+QByteArray::number(samples_.load())+"\n",&problem) || !file_->flush())
                throw std::runtime_error("Could not finish the incomplete compressed file.");
        }
    } catch(const std::exception &ex) { setError(QString("Loop CSV write failed: %1").arg(QString::fromLocal8Bit(ex.what()))); }
    if(file_->isOpen()) { file_->flush(); file_->close(); }
    { std::lock_guard<std::mutex> lock(mutex_); queue_.clear(); queuedBytes_=0; }
    elapsedMicros_=micros(Clock::now()-started); running_=false;
}
}}
