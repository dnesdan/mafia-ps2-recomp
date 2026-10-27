#include "mafia/verified_elf.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

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
template<class F> void rejects(F &&f) {
    bool rejected=false;
    try {f();} catch(const std::runtime_error &) {rejected=true;}
    require(rejected,"Invalid ELF unexpectedly accepted");
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
        std::cout<<"Host verification tests passed (synthetic ELF only; no game execution).\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
