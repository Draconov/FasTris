#include "music_startup.hpp"
#include <cassert>
#include <iostream>

using namespace fasttris::app;

int main() {
    MusicStartupState state;
    assert(!state.claimStart());

    state.markFirstFramePresented();
    state.setLifecycleSuspended(true);
    assert(!state.claimStart());

    state.setLifecycleSuspended(false);
    assert(state.claimStart());
    assert(state.starting());
    assert(!state.claimStart());

    state.finish(true);
    assert(state.ready());
    assert(!state.starting());
    assert(!state.claimStart());

    MusicStartupState failed;
    failed.markFirstFramePresented();
    assert(failed.claimStart());
    failed.finish(false);
    assert(failed.failed());
    assert(!failed.claimStart());

    std::cout << "music startup tests passed\n";
}
