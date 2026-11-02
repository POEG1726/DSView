// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QFile>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace pv { namespace data {

// Receives the same cross-channel, 64-sample interleaving as LogicSnapshot.
// A single capture thread calls submit/finish; parallel encoders feed one ordered writer.
class LoopCsvRecorder {
public:
    using ErrorHandler = std::function<void(const QString &)>;
    static const uint64_t DefaultSegmentBytes = 1024ull * 1024 * 1024;
    static const size_t DefaultBlockBytes = 4u * 1024 * 1024;
    static const size_t DefaultQueueBytes = 256u * 1024 * 1024;

    LoopCsvRecorder();
    ~LoopCsvRecorder();
    bool start(const QString &directory, uint64_t rate, const QStringList &channels,
               ErrorHandler errorHandler, QString *error,
               uint64_t segmentBytes = DefaultSegmentBytes,
               size_t blockBytes = DefaultBlockBytes, size_t queueBytes = DefaultQueueBytes,
               uint64_t segmentSamples = 3000000000ull, unsigned encoderThreads = 0);
    bool submit(const void *bytes, size_t size);
    // Mark intent before stopping the producer. Continue accepting its final packets.
    void markCaptureStopRequested() { stopRequested_=true; }
    void finish();
    void fail(const QString &error);
    void wait();
    bool running() const { return running_.load(); }
    bool failed() const { return failed_.load(); }
    uint64_t elapsedMicros() const { return elapsedMicros_.load(); }
    uint64_t samplesWritten() const { return samples_.load(); }
    uint64_t sampleRate() const { return rate_; }
    uint64_t bytesWritten() const { return bytes_.load(); }
    size_t queuedBytes() const { return queuedBytes_.load(); }
    unsigned segmentCount() const { return segments_.load(); }
    QString outputDirectory() const { return runDir_; }
    unsigned encoderThreads() const { return encoderThreads_; }
    uint64_t formatMicros() const { return formatMicros_.load(); }
    uint64_t compressionMicros() const { return compressionMicros_.load(); }
    size_t finalTailBytes() const { return tailBytes_.load(); }
    size_t finalTailSamples() const { return tailSamples_.load(); }
    size_t omittedTailBytes() const { return omittedTailBytes_.load(); }

private:
    enum { FooterReserveBytes = 256 };
    void run();
    void setError(const QString &error);
    bool queueBlock(std::vector<uint8_t> &&block, bool finalBlock = false);
    bool openSegment(uint64_t startIndex, QString *error);
    bool closeSegment(uint64_t endIndex, QString *error);
    struct EncodedPage {
        uint64_t first = 0, end = 0, textBytes = 0;
        QByteArray firstRow, compressed;
        bool firstChanged = false;
    };
    struct EncodedChunk {
        std::vector<EncodedPage> pages;
        uint64_t formatMicros = 0, compressionMicros = 0;
    };
    EncodedChunk encodeChunk(const std::shared_ptr<std::vector<uint8_t>> &raw,
                            uint64_t base, uint64_t previous, size_t pageLimit,
                            uint64_t validSamples) const;
    bool writeBytes(const QByteArray &bytes, QString *error);
    bool writeText(const QByteArray &text, QString *error);
    bool writePage(const EncodedPage &page, QString *error);
    QByteArray row(uint64_t index, uint64_t value) const;
    int formatRow(char *buffer, uint64_t index, uint64_t value) const;

    QString runDir_;
    QStringList channels_;
    uint64_t rate_ = 0, segmentLimit_ = 0, segmentSamples_ = 0;
    size_t blockLimit_ = 0, queueLimit_ = 0;
    ErrorHandler errorHandler_;
    std::thread worker_;
    std::mutex mutex_;
    std::mutex producerMutex_;
    std::condition_variable condition_;
    std::deque<std::vector<uint8_t>> queue_;
    std::vector<uint8_t> producer_;
    bool finish_ = false, ready_ = false, accepting_ = false;
    QString error_;
    std::atomic<bool> failed_{false}, running_{false}, drainOnFailure_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<size_t> tailBytes_{0}, tailSamples_{0}, omittedTailBytes_{0};
    std::atomic<uint64_t> samples_{0}, bytes_{0}, elapsedMicros_{0}, formatMicros_{0}, compressionMicros_{0};
    unsigned encoderThreads_ = 1;
    unsigned fractionDigits_ = 0;
    uint64_t fractionScale_ = 0;
    std::vector<QByteArray> channelSuffixes_;
    std::atomic<size_t> queuedBytes_{0};
    std::atomic<unsigned> segments_{0};
    QFile *file_ = nullptr;
    QString partPath_, finalPath_, startedUtc_;
    uint64_t segmentBytes_ = 0, segmentStart_ = 0;
    bool segmentHasRow_ = false;
};
}}
