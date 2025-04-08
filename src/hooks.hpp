#pragma once

#include "libmem/libmem.h"

#include <vector>

namespace Hooks
{
	void setup();
	bool checkAddresses(std::vector<lm_address_t>);
}
