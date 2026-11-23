#pragma once
// Search screen: type a song on the board, the PC bridge searches Spotify, tap a result to play it on the PC.
namespace search_ui {
void begin();   // call once, right after ui::build(), from the same context as ui::build()
}
