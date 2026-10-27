#include "mafia/verified_elf.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace mafia {
namespace {
constexpr std::uint64_t max_elf_size = 256u * 1024u * 1024u;
constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

void compress(std::array<std::uint32_t, 8> &state, const std::uint8_t *block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = (std::uint32_t{block[i*4]} << 24) |
                   (std::uint32_t{block[i*4+1]} << 16) |
                   (std::uint32_t{block[i*4+2]} << 8) | block[i*4+3];
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const auto a = words[i-15], b = words[i-2];
        words[i] = words[i-16] + (std::rotr(a,7)^std::rotr(a,18)^(a>>3)) +
                   words[i-7] + (std::rotr(b,17)^std::rotr(b,19)^(b>>10));
    }
    auto [a,b,c,d,e,f,g,h] = state;
    for (std::size_t i = 0; i < 64; ++i) {
        const auto t1 = h + (std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25)) +
                        ((e&f)^(~e&g)) + round_constants[i] + words[i];
        const auto t2 = (std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22)) +
                        ((a&b)^(a&c)^(b&c));
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size()-offset < 4)
        throw std::runtime_error("ELF field exceeds file bounds");
    return bytes[offset] | (std::uint32_t{bytes[offset+1]}<<8) |
           (std::uint32_t{bytes[offset+2]}<<16) | (std::uint32_t{bytes[offset+3]}<<24);
}
std::uint16_t little16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size()-offset < 2)
        throw std::runtime_error("ELF field exceeds file bounds");
    return static_cast<std::uint16_t>(bytes[offset] | (std::uint16_t{bytes[offset+1]}<<8));
}
std::uint32_t physical_address(std::uint32_t address) {
    return address >= 0x80000000u && address < 0xc0000000u ? address & 0x1fffffffu : address;
}
} // namespace

std::string sha256(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max()/8)
        throw std::runtime_error("SHA-256 input is too large");
    std::array<std::uint32_t,8> state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const auto full = bytes.size() / 64 * 64;
    for (std::size_t i=0; i<full; i+=64) compress(state, bytes.data()+i);
    std::array<std::uint8_t,128> tail{};
    const auto left=bytes.size()-full;
    std::copy(bytes.begin()+static_cast<std::ptrdiff_t>(full), bytes.end(), tail.begin());
    tail[left]=0x80;
    const std::size_t padded = left<56 ? 64 : 128;
    const auto bit_size=static_cast<std::uint64_t>(bytes.size())*8;
    for (std::size_t i=0;i<8;++i) tail[padded-1-i]=static_cast<std::uint8_t>(bit_size>>(i*8));
    compress(state,tail.data());
    if(padded==128) compress(state,tail.data()+64);
    constexpr char hex[]="0123456789abcdef";
    std::string result;
    result.reserve(64);
    for(auto word:state) for(int shift=28;shift>=0;shift-=4) result+=hex[(word>>shift)&15u];
    return result;
}

VerifiedElf verify_elf(std::span<const std::uint8_t> bytes,
                       std::string_view expected_sha256, std::uint32_t expected_entry) {
    if(expected_sha256.size()!=64 || !std::all_of(expected_sha256.begin(), expected_sha256.end(),
       [](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))
        throw std::runtime_error("Build profile has an invalid ELF SHA-256");
    if(bytes.size()<52 || bytes.size()>max_elf_size)
        throw std::runtime_error("ELF size is outside the supported 52-byte to 256-MiB range");
    const auto actual=sha256(bytes);
    if(actual!=expected_sha256) throw std::runtime_error("ELF SHA-256 mismatch: expected "+
        std::string(expected_sha256)+", received "+actual);
    if(bytes[0]!=0x7f || bytes[1]!='E' || bytes[2]!='L' || bytes[3]!='F' ||
       bytes[4]!=1 || bytes[5]!=1 || bytes[6]!=1 || little16(bytes,16)!=2 ||
       little16(bytes,18)!=8 || little32(bytes,20)!=1 || little16(bytes,40)!=52)
        throw std::runtime_error("Expected an ELF32 little-endian MIPS ET_EXEC image");
    VerifiedElf result;
    result.sha256=actual;
    result.entry=little32(bytes,24);
    if(result.entry!=expected_entry || (result.entry&3u)!=0)
        throw std::runtime_error("ELF entry point does not match the generated build profile");
    const auto phoff=little32(bytes,28);
    const auto phsize=little16(bytes,42);
    const auto phcount=little16(bytes,44);
    if(phcount==0 || phcount==0xffff || phsize!=32 || std::uint64_t{phoff}+std::uint64_t{phsize}*phcount>bytes.size())
        throw std::runtime_error("ELF program-header table is missing or truncated");
    bool executable_entry=false;
    for(std::uint32_t i=0;i<phcount;++i) {
        const auto at=static_cast<std::size_t>(phoff)+static_cast<std::size_t>(i)*phsize;
        if(little32(bytes,at)!=1) continue;
        LoadSegment segment{little32(bytes,at+4),little32(bytes,at+8),little32(bytes,at+16),
                            little32(bytes,at+20),little32(bytes,at+24)};
        if(segment.file_size>segment.memory_size ||
           std::uint64_t{segment.offset}+segment.file_size>bytes.size())
            throw std::runtime_error("ELF load segment exceeds file bounds or its memory size");
        const std::uint64_t physical=physical_address(segment.virtual_address);
        if(physical+segment.memory_size>0x2000000u ||
           std::uint64_t{segment.virtual_address}+segment.memory_size>0x100000000ull)
            throw std::runtime_error("ELF load segment exceeds 32 MiB EE RAM");
        const auto alignment=little32(bytes,at+28);
        if(alignment>1 && ((alignment&(alignment-1))!=0 ||
                            ((segment.virtual_address-segment.offset)&(alignment-1))!=0))
            throw std::runtime_error("ELF load segment has invalid file/virtual alignment");
        if(segment.memory_size!=0) for(const auto &other:result.segments) {
            const std::uint64_t other_start=physical_address(other.virtual_address);
            if(physical<other_start+other.memory_size && other_start<physical+segment.memory_size)
                throw std::runtime_error("ELF load segments overlap physical memory");
        }
        const std::uint64_t entry=physical_address(result.entry);
        if((segment.flags&1u)!=0 && entry>=physical && entry+4<=physical+segment.file_size)
            executable_entry=true;
        if(segment.memory_size!=0) result.segments.push_back(segment);
    }
    if(result.segments.empty() || !executable_entry)
        throw std::runtime_error("ELF entry is not backed by an executable PT_LOAD segment");
    result.bytes.assign(bytes.begin(),bytes.end());
    return result;
}

VerifiedElf read_verified_elf(const std::filesystem::path &path,
                            std::string_view expected_sha256, std::uint32_t expected_entry) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("Cannot open ELF: "+path.string());
    const auto size=file.tellg();
    if(size<52 || static_cast<std::uint64_t>(size)>max_elf_size)
        throw std::runtime_error("ELF size is outside the supported range");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),size))
        throw std::runtime_error("ELF read failed: "+path.string());
    return verify_elf(bytes,expected_sha256,expected_entry);
}

std::string json_string(std::string_view value) {
    constexpr char hex[]="0123456789abcdef";
    std::string out="\"";
    for(unsigned char c:value) {
        if(c=='\"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15u];}
        else out+=static_cast<char>(c);
    }
    return out+'\"';
}
} // namespace mafia
