#include "test_framework.h"
#include "bringup/serial_tx_queue.h"
#include <string>
#include <vector>
#include <algorithm>
using namespace BroTracker;
namespace {
std::string Drain(SerialTxQueue& q,unsigned quantum){std::string out;while(q.Count()){std::size_t n;const char* p=q.Data(n);n=std::min<std::size_t>(n,quantum);out.append(p,n);q.Consume(n);}return out;}
}
TEST_CASE(TxByteRingPreservesPartialLinesAndWrapOrder) {
    SerialTxQueue q;std::string expected;
    for(unsigned round=0;round<100;++round){
        for(unsigned i=0;i<9;++i){std::string line(200+(i*37)%300,char('a'+i));line+='\n';CHECK(q.Push(line.c_str()));expected+=line;}
        std::string out;while(q.Count()){std::size_t n;const auto* p=q.Data(n);n=std::min<std::size_t>(n,7);out.append(p,n);q.Consume(n);}
        CHECK(out==expected);CHECK(q.Used()==0);CHECK(!q.Partial());expected.clear();
    }
    CHECK(q.Rejected()==0);
}
TEST_CASE(TxFullBytesDeferProducerWithoutLosingOrConsumingReply) {
    SerialTxQueue q;std::vector<std::string> pending;
    for(unsigned i=0;i<40;++i)pending.push_back(std::to_string(i)+std::string(600,'x')+"\n");
    unsigned producer=0,deferrals=0;std::string expected,out;
    while(producer<pending.size()||q.Count()){
        if(producer<pending.size()){
            const auto& line=pending[producer];
            if(q.Room(1,line.size())){CHECK(q.Push(line.c_str()));expected+=line;++producer;}
            else ++deferrals;
        }
        // Simulate tiny USB writes, including turns with no space at all.
        if(deferrals%3 || producer==pending.size()) {std::size_t n;const auto* p=q.Data(n);n=std::min<std::size_t>(n,13);if(n){out.append(p,n);q.Consume(n);}}
        else if(!q.Room(1,604))++deferrals;
    }
    CHECK(out==expected);CHECK(deferrals>0);CHECK(q.Rejected()==0);CHECK(q.PeakBytes()<=q.kBytes);
}
TEST_CASE(TxDescriptorSaturationAndDisconnectResetRetainStatistics) {
    SerialTxQueue q;for(unsigned i=0;i<51;++i)CHECK(q.Push("a\n"));
    CHECK(!q.Room(1,2));CHECK(q.PeakMessages()==51);CHECK(q.Used()==102);
    std::size_t n;q.Data(n);q.Consume(1);CHECK(q.Partial());
    q.Clear();CHECK(q.Count()==0);CHECK(q.Used()==0);CHECK(q.PeakMessages()==51);
    CHECK(q.Push("new\n"));CHECK(Drain(q,1)=="new\n");
}
TEST_CASE(TxWorstLongPathPageFitsBatchReservationWithAckCoexistence) {
    SerialTxQueue q;std::string expected;
    auto add=[&](std::string s){expected+=s;CHECK(q.Push(s.c_str()));};
    CHECK(q.Room(19,4096));
    add("BTSTORE1 WAVDIR 4 4294967295 @"+std::string(510,'a')+"\n");
    add("BTSTORE1 WAVINFO 4 4294967295 0\n");
    for(unsigned i=0;i<8;++i){
        add("BTSTORE1 WAVITEM 4 4294967295 F "+std::string(251,'a')+".wav\n");
        add("BTSTORE1 WAVMETA 4 4294967295 "+std::to_string(i)+" 0 1 1 16 44100\n");
    }
    add("BTSTORE1 WAVEND 4 4294967295 4294967264 0 64\n");
    CHECK(q.Used()<4096);CHECK(q.Room(4,2560));
    add("BTTEST1 STOPPED\n");add("BTMUTE1 APPLIED 1 4294967295\n");
    CHECK(Drain(q,17)==expected);CHECK(q.Rejected()==0);
}
TEST_CASE(TxOverlongLineIsRejectedAtomically) {
    SerialTxQueue q;CHECK(q.Push("first\n"));CHECK(!q.Push(std::string(640,'x').c_str()));
    CHECK(q.Count()==1);CHECK(Drain(q,64)=="first\n");CHECK(q.Rejected()==1);
}
