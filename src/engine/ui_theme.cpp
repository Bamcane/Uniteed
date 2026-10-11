#include "engine/ui_theme.h"

/*
	The theme is defined in its own translation unit so the interface colours
	are available without the widget set: the editor model reads them for the
	clear colour, and the headless build links that model without a painter.
*/

namespace engine::ui
{
	const CTheme Theme;
} // namespace engine::ui
