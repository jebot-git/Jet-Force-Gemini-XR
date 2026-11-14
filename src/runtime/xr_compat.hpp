#pragma once
namespace recompui {
// Engine compatibility names; these APIs have no desktop implementation.
void message_box(const char *message);
void get_window_size(int &width,int &height);
}
