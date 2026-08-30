#include "fasttris/game.hpp"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <iostream>
int main(int argc,char**argv){int n=argc>1?std::max(1000,std::atoi(argv[1])):500000;fasttris::Rules r;r.handling.lock_delay_ms=0;fasttris::Game g(1,fasttris::Mode::Zen,r);std::uint64_t seed=1;auto begin=std::chrono::steady_clock::now();for(int i=0;i<n;++i){if(g.gameOver()){g.restart(++seed,fasttris::Mode::Zen);}switch(i%8){case 0:g.press(fasttris::Action::RotateCW);break;case 1:g.press(fasttris::Action::RotateCCW);break;case 2:g.press(fasttris::Action::Rotate180);break;case 3:g.press(fasttris::Action::Left);g.release(fasttris::Action::Left);break;case 4:g.press(fasttris::Action::Right);g.release(fasttris::Action::Right);break;default:break;}g.press(fasttris::Action::HardDrop);}auto end=std::chrono::steady_clock::now();double sec=std::chrono::duration<double>(end-begin).count();std::cout<<n<<" lock cycles in "<<sec<<" s\n"<<(n/sec)<<" cycles/s\n";return 0;}
