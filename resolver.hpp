#pragma once
#include "../../includes.hpp"
class resolver : public singleton<resolver>
{
	public:

	void resolve_angles(c_base_player* pEnt);
	void store_freestand();
};

