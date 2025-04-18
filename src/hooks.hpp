#pragma once

#include "libmem/libmem.h"

#include <vector>

namespace Hooks
{
	bool setup();
	bool checkAddresses(std::vector<lm_address_t>);
}
