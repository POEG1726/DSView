// SPDX-License-Identifier: GPL-3.0-or-later
#include "../DSView/pv/sigsession.h"
#include <QCoreApplication>
#include <atomic>
#include <thread>
#include <cstdio>

static void load(pv::data::LogicSnapshot *snapshot, unsigned char value) {
    // Match the application's capture setup before first_payload: the constructor
    // alone does not initialize the interleaved writer's partial-word state.
    snapshot->clear();
    sr_channel channel = {}; channel.type=SR_CHANNEL_LOGIC; channel.enabled=TRUE; channel.index=0;
    GSList *channels=g_slist_append(NULL,&channel);
    uint8_t samples[8]; for (unsigned i=0;i<8;i++) samples[i]=value;
    sr_datafeed_logic logic={}; logic.format=LA_CROSS_DATA; logic.length=sizeof(samples); logic.data=samples;
    snapshot->first_payload(logic,64,channels,true);
    snapshot->capture_ended();
    g_slist_free(channels);
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    pv::SigSession session;
    auto *old=static_cast<pv::data::LogicSnapshot *>(session.get_snapshot(SR_CHANNEL_LOGIC));
    load(old,0xaa);
    pv::SessionData *owned=session.pin_export_data();
    if (!owned || session.pin_export_data()) return 1;
    pv::SessionData *current=owned;
    session.clear_session_data(current);
    auto *next=static_cast<pv::data::LogicSnapshot *>(session.get_snapshot(SR_CHANNEL_LOGIC));
    if (current==owned || next==old || next!=current->get_logic()) return 2;
    std::atomic<bool> failed(false);
    std::thread reader([&](){ for(int k=0;k<20000;k++) for(int i=0;i<64;i++) if(old->get_sample(i,0)!=(bool)(i&1)) failed=true; });
    load(next,0x55);
    reader.join();
    if (failed || old->get_sample_count()!=64 || next->get_sample_count()!=64) return 3;
    for(int i=0;i<64;i++) if(next->get_sample(i,0)==old->get_sample(i,0)) return 4;
    session.release_export_data();
    if(next!=session.get_snapshot(SR_CHANNEL_LOGIC) || next->get_sample_count()!=64) return 5;
    pv::SessionData *same=session.pin_export_data();
    session.release_export_data();
    if(same!=current || next->get_sample_count()!=64) return 6;
    std::puts("Pinned old data survived concurrent reads and new snapshot creation; release paths passed.");
    return 0;
}
