// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <set>
#include <list>
#include <map>
#include <thread>
#include <mutex>
#include <memory>
#include <libsigrok.h>
#include "../DSView/pv/data/loopcsvrecorder.h"
#define private public
#include "../DSView/pv/sigsession.h"
#undef private
#include <cstdio>
#include "compressed_csv_test_utils.h"
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir tmp;if(!tmp.isValid())return 1;
    pv::SigSession session;session._data_lock=true;session._loop_csv.reset(new pv::data::LoopCsvRecorder());
    QString error;bool failed=false;
    if(!session._loop_csv->start(tmp.path(),1000,{"A"},[&](const QString &){failed=true;},&error,1024*1024,64,1024*1024))return 2;
    uint8_t data[8]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa};sr_datafeed_logic logic={};logic.data=data;logic.length=8;logic.format=LA_CROSS_DATA;
    sr_datafeed_packet packet={};packet.type=SR_DF_LOGIC;packet.status=SR_PKT_OK;packet.payload=&logic;
    session.data_feed_in(reinterpret_cast<const sr_dev_inst *>(1),&packet);
    session._loop_csv->finish();session._loop_csv->wait();
    if(failed || session._loop_csv->samplesWritten()!=64)return 3;
    QDir out(session._loop_csv->outputDirectory());
    bool complete=false;
    const QByteArray content=readCompressedCsv(out.filePath("capture_000001.csv.zst"),&complete);if(!complete)return 4;
    if(!content.contains("0,0,0\n")||!content.contains("1,0.001,1\n"))return 5;
    session._loop_csv.reset(new pv::data::LoopCsvRecorder());failed=false;
    if(!session._loop_csv->start(tmp.path(),1000,{"A"},[&](const QString &){failed=true;},&error,1024*1024,64,1024*1024))return 6;
    packet.type=SR_DF_OVERFLOW;packet.payload=nullptr;session.data_feed_in(reinterpret_cast<const sr_dev_inst *>(1),&packet);session._loop_csv->wait();
    if(!failed||!session._loop_csv->failed())return 7;QDir bad(session._loop_csv->outputDirectory());if(bad.entryList({"*.part"},QDir::Files).isEmpty())return 8;
    // Exercise the session's stop-request path with no device/collection thread.
    session._loop_csv.reset(new pv::data::LoopCsvRecorder());failed=false;
    if(!session._loop_csv->start(tmp.path(),50000000,{"A","B","C","D","E","F"},[&](const QString &){failed=true;},&error))return 9;
    uint8_t tail[41]={0};logic.data=tail;logic.length=sizeof(tail);packet.type=SR_DF_LOGIC;packet.status=SR_PKT_OK;packet.payload=&logic;
    session.data_feed_in(reinterpret_cast<const sr_dev_inst *>(1),&packet);
    session._is_working=true;session._bClose=true;
    if(!session.action_stop_capture())return 10;
    session._loop_csv->finish();session._loop_csv->wait();
    if(failed || session._loop_csv->samplesWritten()!=8 || session._loop_csv->finalTailSamples()!=8)return 11;
    bool stoppedComplete=false;
    const QByteArray stopped=readCompressedCsv(QDir(session._loop_csv->outputDirectory()).filePath("capture_000001.csv.zst"),&stoppedComplete);
    if(!stoppedComplete || !stopped.contains("; End sample (exclusive): 8\n"))return 12;
    std::puts("Session packet hook records while display is locked, preserves overflow errors and marks requested stops before finalization.");return 0;
}
