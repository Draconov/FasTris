#include "fasttris/sha256.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace fasttris {
namespace {
constexpr std::array<std::uint32_t,64> K={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
inline std::uint32_t rotr(std::uint32_t x,int n){return (x>>n)|(x<<(32-n));}
inline int hexNibble(char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
}

Sha256::Sha256()
    : state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19} {}

void Sha256::transform(const std::uint8_t block[64]) {
    std::uint32_t w[64]{};
    for(int i=0;i<16;++i){
        w[i]=(std::uint32_t(block[4*i])<<24)|(std::uint32_t(block[4*i+1])<<16)|
             (std::uint32_t(block[4*i+2])<<8)|std::uint32_t(block[4*i+3]);
    }
    for(int i=16;i<64;++i){
        const auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
        const auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for(int i=0;i<64;++i){
        const auto S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
        const auto ch=(e&f)^((~e)&g);
        const auto t1=h+S1+ch+K[i]+w[i];
        const auto S0=rotr(a,2)^rotr(a,13)^rotr(a,22);
        const auto maj=(a&b)^(a&c)^(b&c);
        const auto t2=S0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
    state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
}

void Sha256::update(const void* data, std::size_t size) {
    if(finalized_ || size==0)return;
    const auto* p=static_cast<const std::uint8_t*>(data);
    total_bytes_+=size;
    while(size>0){
        const std::size_t n=std::min<std::size_t>(64-buffer_size_,size);
        std::memcpy(buffer_.data()+buffer_size_,p,n);
        buffer_size_+=n;p+=n;size-=n;
        if(buffer_size_==64){transform(buffer_.data());buffer_size_=0;}
    }
}

std::array<std::uint8_t,32> Sha256::finalBytes() {
    if(finalized_)return digest_;
    const std::uint64_t bits=total_bytes_*8u;
    buffer_[buffer_size_++]=0x80;
    if(buffer_size_>56){
        while(buffer_size_<64)buffer_[buffer_size_++]=0;
        transform(buffer_.data());buffer_size_=0;
    }
    while(buffer_size_<56)buffer_[buffer_size_++]=0;
    for(int i=7;i>=0;--i)buffer_[buffer_size_++]=static_cast<std::uint8_t>(bits>>(i*8));
    transform(buffer_.data());buffer_size_=0;
    for(std::size_t i=0;i<state_.size();++i){
        digest_[4*i]=static_cast<std::uint8_t>(state_[i]>>24);
        digest_[4*i+1]=static_cast<std::uint8_t>(state_[i]>>16);
        digest_[4*i+2]=static_cast<std::uint8_t>(state_[i]>>8);
        digest_[4*i+3]=static_cast<std::uint8_t>(state_[i]);
    }
    finalized_=true;
    return digest_;
}

std::string hexLower(const std::uint8_t* data,std::size_t size){
    static constexpr char lut[]="0123456789abcdef";
    std::string out(size*2,'0');
    for(std::size_t i=0;i<size;++i){out[2*i]=lut[data[i]>>4];out[2*i+1]=lut[data[i]&15];}
    return out;
}

std::string Sha256::finalHex(){const auto d=finalBytes();return hexLower(d.data(),d.size());}
std::string sha256(std::string_view data){Sha256 h;h.update(data);return h.finalHex();}

bool parseHex32(std::string_view hex,std::array<std::uint8_t,32>& out){
    if(hex.size()!=64)return false;
    for(std::size_t i=0;i<32;++i){
        const int hi=hexNibble(hex[2*i]),lo=hexNibble(hex[2*i+1]);
        if(hi<0||lo<0)return false;
        out[i]=static_cast<std::uint8_t>((hi<<4)|lo);
    }
    return true;
}

} // namespace fasttris
