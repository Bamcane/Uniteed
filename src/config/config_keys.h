#ifndef CONFIG_CONFIG_KEYS_H
#define CONFIG_CONFIG_KEYS_H

#include <string>

#include "config/config.h"

namespace config
{
	// window
	inline const key<int> WindowWidth{"uniteed_window_width", 1360};
	inline const key<int> WindowHeight{"uniteed_window_height", 860};

	// editor
	inline const key<std::string> DefaultTarget{"uniteed_default_target", "08"};
	inline const key<bool> ShowGrid{"uniteed_show_grid", true};
	inline const key<bool> ShowDetail{"uniteed_show_detail", true};
	inline const key<int> BrushSize{"uniteed_brush_size", 1};
	inline const key<std::string> LastMap{"uniteed_last_map", ""};
	inline const key<std::string> AssetVersion{"uniteed_asset_version", ""};

	// logging: trace, info, warn or error
	inline const key<std::string> LogLevel{"uniteed_log_level", "info"};
} // namespace config

#endif // CONFIG_CONFIG_KEYS_H
