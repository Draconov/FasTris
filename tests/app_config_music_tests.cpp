#include "app_config.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace fasttris::app;

int main() {
    const auto path = std::filesystem::temp_directory_path() / "fastris_music_config_test.cfg";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    AppConfig cfg = defaultConfig();
    assert(cfg.music_volume == 70);
    cfg.music_volume = 37;
    assert(saveConfig(path.string(), cfg));

    AppConfig loaded = defaultConfig();
    assert(loadConfig(path.string(), loaded));
    assert(loaded.music_volume == 37);

    {
        std::ofstream out(path);
        out << "musicvolume=999\n";
    }
    loaded = defaultConfig();
    assert(loadConfig(path.string(), loaded));
    assert(loaded.music_volume == 100);

    loaded.music_volume = 12;
    resetSettings(loaded);
    assert(loaded.music_volume == 70);

    std::filesystem::remove(path, ec);
    std::cout << "app config music tests passed\n";
}
