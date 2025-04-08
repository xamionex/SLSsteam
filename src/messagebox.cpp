#include "messagebox.hpp"
#include "boxer/boxer.h"
#include <algorithm>
#include <thread>

void MessageBox::show()
{
	boxer::show(text.c_str(), title.c_str());

	auto fd = std::find(messageBoxes.begin(), messageBoxes.end(), this);
	if (fd == messageBoxes.end())
		return;

	messageBoxes.erase(fd);
	delete this;
}

void MessageBox::showAsync()
{
	messageBoxes.emplace_back(this);
	std::thread(&MessageBox::show, this);
}
