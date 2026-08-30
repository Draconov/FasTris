#include "fasttris/bag.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
int main(int argc,char**argv){std::uint64_t seed=argc>1?std::strtoull(argv[1],nullptr,10):1;int count=argc>2?std::max(1,std::atoi(argv[2])):70;fasttris::Bag7 bag(seed);std::cout<<"seed "<<seed<<"\n";for(int i=0;i<count;++i){std::cout<<fasttris::pieceName(bag.pop());if((i+1)%7==0)std::cout<<'\n';else std::cout<<' ';}return 0;}
