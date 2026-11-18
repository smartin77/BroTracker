#pragma once
#include "wav_buffer_loader.h"
namespace BroTracker {
// Foreground-only, at most one Size or seek/read pair per Step. PCM is never
// read here. Unknown chunks are skipped, with the same 1024-chunk bound as boot.
class WavInspectionJob {
public:
    void Begin(bool stereo=false,bool lower_rates=false,bool pcm8_and_8363=false) {
        *this=WavInspectionJob{};stereo_=stereo;lower_rates_=lower_rates;pcm8_and_8363_=pcm8_and_8363;
    }
    bool Done()const{return phase_==DonePhase;}
    WavBufferStatus Status()const{return status_;}
    const WavMetadata& Metadata()const{return metadata_;}
    void Step(WavBufferReader& reader) {
        using namespace WavBufferDetail;
        std::uint8_t bytes[18]{};
        if(Done())return;
        if(phase_==Size) {
            if(!reader.Size(metadata_.file_bytes))return Finish(WavBufferStatus::IoError);
            metadata_.file_known=true;
            if(metadata_.file_bytes<12)return Finish(WavBufferStatus::Truncated);
            phase_=Header;return;
        }
        if(phase_==Header) {
            if(!ReadAt(reader,0,bytes,12))return Finish(WavBufferStatus::IoError);
            if(std::memcmp(bytes,"RIFF",4) || std::memcmp(bytes+8,"WAVE",4))return Finish(WavBufferStatus::InvalidContainer);
            end_=std::uint64_t(U32(bytes+4))+8;
            if(end_<12)return Finish(WavBufferStatus::InvalidContainer);
            if(end_>metadata_.file_bytes)return Finish(WavBufferStatus::Truncated);
            offset_=12;phase_=Chunk;return;
        }
        if(phase_==Format) {
            if(!ReadAt(reader,payload_,bytes,chunk_bytes_==16?16:18))return Finish(WavBufferStatus::IoError);
            if(chunk_bytes_!=16 && U16(bytes+16)!=chunk_bytes_-18)return Finish(WavBufferStatus::InvalidFormat);
            metadata_.format=U16(bytes);metadata_.channels=U16(bytes+2);metadata_.sample_rate=U32(bytes+4);
            metadata_.byte_rate=U32(bytes+8);metadata_.block_align=U16(bytes+12);metadata_.bits=U16(bytes+14);metadata_.format_known=true;
            supported_=SupportsIntegerPcm(metadata_,stereo_,lower_rates_,pcm8_and_8363_);
            if(supported_ && !ValidPcmGeometry(metadata_))return Finish(WavBufferStatus::InvalidFormat);
            phase_=Chunk;return;
        }
        if(offset_==end_)return Validate();
        if(chunks_++>=kWavInspectionChunkLimit)return Finish(WavBufferStatus::ChunkLimit);
        if(end_-offset_<8)return Finish(WavBufferStatus::InvalidChunk);
        if(!ReadAt(reader,offset_,bytes,8))return Finish(WavBufferStatus::IoError);
        chunk_bytes_=U32(bytes+4);payload_=offset_+8;
        if(chunk_bytes_>end_-payload_ || (chunk_bytes_&1)>end_-payload_-chunk_bytes_)return Finish(WavBufferStatus::InvalidChunk);
        offset_=payload_+chunk_bytes_+(chunk_bytes_&1);
        if(!std::memcmp(bytes,"fmt ",4)) {
            if(have_format_)return Finish(WavBufferStatus::DuplicateChunk);
            have_format_=true;
            if(chunk_bytes_<16 || chunk_bytes_==17)return Finish(WavBufferStatus::InvalidFormat);
            phase_=Format;
        } else if(!std::memcmp(bytes,"data",4)) {
            if(have_data_)return Finish(WavBufferStatus::DuplicateChunk);
            have_data_=true;metadata_.data_offset=payload_;metadata_.data_bytes=chunk_bytes_;metadata_.data_known=true;
        }
    }
private:
    void Finish(WavBufferStatus status){status_=status;phase_=DonePhase;}
    void Validate() {
        if(!have_format_ || !have_data_)return Finish(WavBufferStatus::MissingChunk);
        if(!metadata_.data_bytes)return Finish(WavBufferStatus::InvalidData);
        const auto align=std::uint64_t(metadata_.channels)*((metadata_.bits+7)/8);
        const bool layout=metadata_.format==1 && metadata_.channels && metadata_.bits && metadata_.bits%8==0 && metadata_.sample_rate &&
            metadata_.block_align==align && metadata_.byte_rate==std::uint64_t(metadata_.sample_rate)*align;
        if(layout) {
            if(metadata_.data_bytes%metadata_.block_align)return Finish(WavBufferStatus::InvalidData);
            metadata_.frames=metadata_.data_bytes/metadata_.block_align;metadata_.duration_us=metadata_.frames*1000000/metadata_.sample_rate;metadata_.duration_known=true;
        }
        if(!supported_)return Finish(WavBufferStatus::UnsupportedFormat);
        if(metadata_.data_bytes%metadata_.block_align)return Finish(WavBufferStatus::InvalidData);
        metadata_.required_pcm_bytes=metadata_.data_bytes;Finish(WavBufferStatus::Success);
    }
    enum Phase{Size,Header,Chunk,Format,DonePhase};Phase phase_=Size;
    WavBufferStatus status_=WavBufferStatus::IoError;WavMetadata metadata_{};
    std::uint64_t end_=0,offset_=0,payload_=0,chunk_bytes_=0;unsigned chunks_=0;
    bool stereo_=false,lower_rates_=false,pcm8_and_8363_=false;
    bool have_format_=false,have_data_=false,supported_=false;
};
}
