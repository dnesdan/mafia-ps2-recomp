#include "mafia/verified_elf.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

void test_frame_capture();

namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
void put16(std::vector<std::uint8_t> &b, std::size_t p, std::uint16_t n) {
    b[p]=static_cast<std::uint8_t>(n); b[p+1]=static_cast<std::uint8_t>(n>>8);
}
void put32(std::vector<std::uint8_t> &b, std::size_t p, std::uint32_t n) {
    for(unsigned i=0;i<4;++i) b[p+i]=static_cast<std::uint8_t>(n>>(i*8));
}
std::vector<std::uint8_t> synthetic_elf() {
    std::vector<std::uint8_t> b(0x104);
    b[0]=0x7f;b[1]='E';b[2]='L';b[3]='F';b[4]=1;b[5]=1;b[6]=1;
    put16(b,16,2);put16(b,18,8);put32(b,20,1);put32(b,24,0x100000);
    put32(b,28,52);put16(b,40,52);put16(b,42,32);put16(b,44,1);
    put32(b,52,1);put32(b,56,0x100);put32(b,60,0x100000);
    put32(b,68,4);put32(b,72,16);put32(b,76,5);
    return b;
}
std::vector<std::uint8_t> symbol_elf() {
    auto b=synthetic_elf();
    b.resize(0x380);
    put32(b,68,0x20);put32(b,72,0x40);
    put32(b,32,0x200);put16(b,46,40);put16(b,48,4);
    // Null section, executable bytes, strings, then four ELF32 symbols.
    put32(b,0x228+4,1);put32(b,0x228+8,6);put32(b,0x228+12,0x100000);
    put32(b,0x228+16,0x100);put32(b,0x228+20,0x20);
    const std::string names("\0outer\0inner\0point\0",19);
    std::copy(names.begin(),names.end(),b.begin()+0x300);
    put32(b,0x250+4,3);put32(b,0x250+16,0x300);put32(b,0x250+20,19);
    put32(b,0x278+4,2);put32(b,0x278+16,0x340);put32(b,0x278+20,64);
    put32(b,0x278+24,2);put32(b,0x278+36,16);
    const auto symbol=[&](std::size_t at,std::uint32_t name,std::uint32_t address,std::uint32_t size) {
        put32(b,at,name);put32(b,at+4,address);put32(b,at+8,size);
        b[at+12]=0x12;put16(b,at+14,1);
    };
    symbol(0x350,1,0x100000,0x18);
    symbol(0x360,7,0x100008,8);
    symbol(0x370,13,0x100018,0);
    return b;
}
template<class F> void rejects(F &&f) {
    bool rejected=false;
    try {f();} catch(const std::runtime_error &) {rejected=true;}
    require(rejected,"Invalid ELF unexpectedly accepted");
}
void test_symbols() {
    const auto lookup=[](const std::vector<std::uint8_t> &bytes,std::uint32_t address) {
        return mafia::resolve_function_symbol(mafia::verify_elf(bytes,mafia::sha256(bytes),0x100000),address);
    };
    require(!lookup(synthetic_elf(),0x100000),"Stripped ELF acquired an invented symbol");
    auto bytes=symbol_elf();
    const auto inner=lookup(bytes,0xa010000c);
    require(inner && inner->name=="inner" && inner->address==0x100008 && inner->offset==4,
            "Explicit containing function or KSEG alias lookup failed");
    const auto outer=lookup(bytes,0x100014);
    require(outer && outer->name=="outer" && outer->offset==20,"Outer function extent was lost");
    const auto point=lookup(bytes,0x80100018);
    require(point && point->name=="point" && point->offset==0,"Zero-size exact function lookup failed");
    require(!lookup(bytes,0x10001c) && !lookup(bytes,0xe010000c) && !lookup(bytes,0x100009),
            "Unknown address acquired a nearby or unsupported-alias symbol");
    bytes[0x370+12]=0x11;
    require(!lookup(bytes,0x100018),"Data symbol was presented as a function");
    bytes=symbol_elf();put16(bytes,0x370+14,0);
    require(!lookup(bytes,0x100018),"Undefined function was presented as executable");
    bytes=symbol_elf();put16(bytes,0x370+14,2);
    require(!lookup(bytes,0x100018),"Function claiming a string-table owner was presented as executable");
    bytes=symbol_elf();put32(bytes,0x228+20,0x18);
    require(!lookup(bytes,0x100018),"Function outside its declared owner section was accepted");
    bytes=symbol_elf();put32(bytes,0x228+16,0x180);
    require(!lookup(bytes,0x100018),"Owner section with a different file mapping was accepted");
    bytes=symbol_elf();put16(bytes,0x370+14,0xff00);
    const auto old_headers=std::vector<std::uint8_t>(bytes.begin()+0x200,bytes.begin()+0x2a0);
    bytes.resize(0x400u+0xff01u*40u);
    std::copy(old_headers.begin(),old_headers.end(),bytes.begin()+0x400);
    put32(bytes,32,0x400);put16(bytes,48,0xff01);
    const auto reserved_header=0x400u+0xff00u*40u;
    std::copy(old_headers.begin()+40,old_headers.begin()+80,bytes.begin()+reserved_header);
    require(!lookup(bytes,0x100018),"Reserved SHN value was interpreted as an ordinary owner index");
    bytes=symbol_elf();put32(bytes,0x370+8,0xfffffff0);
    require(!lookup(bytes,0x100018),"Overflowing function extent was accepted");
    bytes=symbol_elf();put32(bytes,0x370+8,0x10);
    require(!lookup(bytes,0x100018),"Symbol beyond executable file bytes was accepted");
    bytes=symbol_elf();put32(bytes,32,0xfffffff0);
    rejects([&]{lookup(bytes,0x100000);});
    bytes=symbol_elf();put32(bytes,0x278+24,4);
    rejects([&]{lookup(bytes,0x100000);});
    bytes=symbol_elf();put32(bytes,0x278+36,8);
    rejects([&]{lookup(bytes,0x100000);});
    bytes=symbol_elf();put32(bytes,0x250+20,0xffffffff);
    rejects([&]{lookup(bytes,0x100000);});
    bytes=symbol_elf();put32(bytes,0x250+20,18);
    rejects([&]{lookup(bytes,0x100018);});
    bytes=symbol_elf();put32(bytes,0x370,19);
    rejects([&]{lookup(bytes,0x100018);});
}
}
int main() {
    try {
        require(mafia::sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA empty");
        const std::vector<std::uint8_t> abc{'a','b','c'};
        require(mafia::sha256(abc)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA abc");
        const std::vector<std::uint8_t> million(1000000,'a');
        require(mafia::sha256(million)=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA multi-block");
        auto b=synthetic_elf();
        const auto digest=mafia::sha256(b);
        const auto elf=mafia::verify_elf(b,digest,0x100000);
        require(elf.entry==0x100000 && elf.segments.size()==1 && elf.segments[0].memory_size==16,"ELF identity/segments");
        rejects([&]{mafia::verify_elf(b,std::string(64,'0'),0x100000);});
        rejects([&]{mafia::verify_elf(b,digest,0x100004);});
        b[4]=2;rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0x100000);});
        b=synthetic_elf();put32(b,68,17);rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0x100000);});
        b=synthetic_elf();put32(b,60,0x1fffffc);rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0x100000);});
        b=synthetic_elf();put32(b,28,0xfffffff0);rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0x100000);});
        b=synthetic_elf();put32(b,76,4);rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0x100000);});
        b=synthetic_elf();put32(b,60,0xe0100000);put32(b,24,0xe0100000);
        rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0xe0100000);});
        b=synthetic_elf();put32(b,80,3);rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0x100000);});
        b=synthetic_elf();put16(b,44,2);put32(b,84,1);put32(b,88,0x100);put32(b,92,0x80100008);
        put32(b,100,4);put32(b,104,16);put32(b,108,4);
        rejects([&]{mafia::verify_elf(b,mafia::sha256(b),0x100000);});
        require(mafia::json_string("a\n\"\\") == "\"a\\u000a\\\"\\\\\"","JSON escaping");
        test_symbols();
        test_frame_capture();
        std::cout<<"Host verification tests passed (synthetic ELF and capture bytes; no game execution).\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
