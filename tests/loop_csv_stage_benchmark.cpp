// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore>
#include <zlib.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdio>
#define private public
#include "../DSView/pv/data/loopcsvrecorder.h"
#undef private
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    pv::data::LoopCsvRecorder r; r.rate_=50000000; r.channels_=QStringList{"A","B","C","CLK","D","CS"};
    QByteArray csv; csv.reserve(160000000);
    const uint64_t rows=4000000;
    auto start=std::chrono::steady_clock::now();
    for(uint64_t i=0;i<rows;i++) csv+=r.row(100000000+i*2,(i&1)?8:0);
    double formatting=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    z_stream z={}; deflateInit2(&z,1,Z_DEFLATED,15+16,8,Z_DEFAULT_STRATEGY);
    std::vector<unsigned char> out(deflateBound(&z,csv.size()));
    z.next_in=reinterpret_cast<Bytef *>(csv.data()); z.avail_in=csv.size();
    z.next_out=out.data(); z.avail_out=out.size();
    start=std::chrono::steady_clock::now(); int code=deflate(&z,Z_FINISH);
    double compressing=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::printf("rows=%llu CSV_MiB=%.1f format_s=%.3f gzip_s=%.3f format_Mrows_s=%.2f gzip_Mrows_s=%.2f compressed_MiB=%.1f code=%d\n",
       (unsigned long long)rows,csv.size()/1048576.,formatting,compressing,rows/formatting/1e6,rows/compressing/1e6,z.total_out/1048576.,code);
    deflateEnd(&z); return code==Z_STREAM_END?0:1;
}
