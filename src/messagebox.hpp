#pragma once

#include <string>
#include <vector>

class MessageBox
{
	int index;
	std::string title;
	std::string text;

public:
	MessageBox(const char* title, const char* text) : title(title), text(text) { }

	void show();
	void showAsync();
};

static std::vector<MessageBox*> messageBoxes;
