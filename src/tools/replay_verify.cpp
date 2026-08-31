#include "fasttris/replay.hpp"
#include "fasttris/sha256.hpp"
#include <iostream>

int main(int argc,char**argv){
    if(argc!=2){std::cerr<<"usage: fastris_verify replay.ftr\n";return 2;}
    fasttris::Replay r;
    std::string err;
    fasttris::Sha256Digest actual{};
    if(!fasttris::loadReplay(argv[1],r,&err)){std::cerr<<"load failed: "<<err<<"\n";return 3;}
    const bool ok=fasttris::verifyReplay(r,&actual);
    std::cout<<(ok?"VERIFIED":"FAILED")<<"\nexpected "
             <<(r.final_hash?fasttris::hexLower(*r.final_hash):std::string("<missing>"))
             <<"\nactual   "<<fasttris::hexLower(actual)<<"\n";
    return ok?0:4;
}
