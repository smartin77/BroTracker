#pragma once
#include "foundation.h"
#include "bringup/btt_export_protocol.h"
namespace btt {
// Foreground only; caller reserves one TX line before Command/Poll. No IO except
// one <=64-byte immutable read per NEXT. Output is copied to the existing FIFO.
// Each call needs a fresh monotonic sample after any preceding command; a cached
// pre-command timestamp can falsely look like unsigned timer wrap in Poll.
class Export {
    ReaderOwner source_;
    uint32_t high_=0,id_=0,generation_=0,length_=0,crc_=0,offset_=0,rolling_=0xffffffffu,at_=0;
    char nonce_[33]{};
    const char* Error(char*,uint32_t,const char*,const char*) noexcept;
public:
    bool Active()const noexcept{return bool(source_);}
    void Disconnect() noexcept {source_.reset();}
    void NewConnection() noexcept {Disconnect();high_=0;}
    BTT_FLASHMEM("export_command") const char* Command(const BroTracker::BttExportRequest&,ReaderOwner,
        uint32_t generation,uint32_t crc,const char* boot,bool ready,uint32_t now,char* reply) noexcept;
    BTT_FLASHMEM("export_poll") bool Poll(uint32_t now,bool ready,char* reply) noexcept;
};
}
