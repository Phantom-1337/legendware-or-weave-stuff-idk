#include "resolver.hpp"
#include "autowall.hpp"

int FreestandSide[64];

void resolver::store_freestand()
{
	if (!g_local->get_active_weapon())
		return;

	if (!g_local->get_active_weapon()->is_weapon())
		return;

	for (int i = 1; i < i::globalvars->m_max_clients; ++i)
	{
		auto player = (c_base_player*)i::entitylist->get_client_entity(i);

		if (!player || !player->is_alive() || player->is_dormant() || player->m_iTeamNum() == g_local->m_iTeamNum())
			continue;

		bool Autowalled = false, HitSide1 = false, HitSide2 = false;
		auto idx = player->ent_index();
		float angToLocal = math::calc_angle(g_local->m_vecOrigin(), player->m_vecOrigin()).y;
		vec3_t ViewPoint = g_local->m_vecOrigin() + vec3_t(0, 0, 90);
		vec2_t Side1 = { (45 * sin(math::deg2rad(angToLocal))), (45 * cos(math::deg2rad(angToLocal))) };
		vec2_t Side2 = { (45 * sin(math::deg2rad(angToLocal + 180))), (45 * cos(math::deg2rad(angToLocal + 180))) };

		vec2_t Side3 = { (50 * sin(math::deg2rad(angToLocal))), (50 * cos(math::deg2rad(angToLocal))) };
		vec2_t Side4 = { (50 * sin(math::deg2rad(angToLocal + 180))), (50 * cos(math::deg2rad(angToLocal + 180))) };

		vec3_t Origin = player->m_vecOrigin();

		vec2_t OriginLeftRight[] = { vec2_t(Side1.x, Side1.y), vec2_t(Side2.x, Side2.y) };

		vec2_t OriginLeftRightLocal[] = { vec2_t(Side3.x, Side3.y), vec2_t(Side4.x, Side4.y) };

		for (int side = 0; side < 2; side++)
		{
			vec3_t OriginAutowall = { Origin.x + OriginLeftRight[side].x, Origin.y - OriginLeftRight[side].y, Origin.z + 90 };
			vec3_t ViewPointAutowall = { ViewPoint.x + OriginLeftRightLocal[side].x, ViewPoint.y - OriginLeftRightLocal[side].y, ViewPoint.z };

			if (autowall::get().CanHitFloatingPoint(OriginAutowall, ViewPoint))
			{
				if (side == 0)
				{
					HitSide1 = true;
					FreestandSide[idx] = -1;
				}
				else if (side == 1)
				{
					HitSide2 = true;
					FreestandSide[idx] = 1;
				}

				Autowalled = true;
			}
			else
			{
				for (int sidealternative = 0; sidealternative < 2; sidealternative++)
				{
					vec3_t ViewPointAutowallalternative = { Origin.x + OriginLeftRight[sidealternative].x, Origin.y - OriginLeftRight[sidealternative].y, Origin.z + 90 };

					if (autowall::get().CanHitFloatingPoint(ViewPointAutowallalternative, ViewPointAutowall))
					{
						if (sidealternative == 0)
						{
							HitSide1 = true;
							FreestandSide[idx] = -1;
						}
						else if (sidealternative == 1)
						{
							HitSide2 = true;
							FreestandSide[idx] = 1;
						}

						Autowalled = true;
					}
				}
			}
		}

	}
}

float get_backward_yaw(c_base_player* player)
{
	return math::calc_angle(g_local->m_vecOrigin(), player->m_vecOrigin()).y;
}

float forward_yaw(c_base_player* player)
{
	return math::normalize_yaw(get_backward_yaw(player) - 180.f);
}

void resolver::resolve_angles(c_base_player* pEnt)
{
	int idx = pEnt->ent_index();
	auto state = pEnt->get_animstate();

	if (!state)
		return;

	state->m_feet_yaw = math::normalize_yaw(pEnt->m_angEyeAngles().y);

	if (pEnt->get_player_info().fake_player)
		return;

	float angle = pEnt->get_eye_pos().y;
	bool forward = fabsf(math::normalize_yaw(math::normalize_yaw(pEnt->m_angEyeAngles().y) - forward_yaw(pEnt))) < 90.f;

	if (g::ragebot::m_missed_shots[idx] == 0) {

		if (forward)
			FreestandSide[idx] *= -1;

		state->m_feet_yaw = math::normalize_yaw(angle - 58.f * FreestandSide[idx]);
	}
	else {
		if (forward) {
			switch (g::ragebot::m_missed_shots[idx] % 2) {
			case 1:

				if (FreestandSide[idx] == 1)
					state->m_feet_yaw = math::normalize_yaw(angle - 58.f);
				else
					state->m_feet_yaw = math::normalize_yaw(angle + 58.f);

				break;
			case 0:

				if (FreestandSide[idx] == 1)
					state->m_feet_yaw = math::normalize_yaw(angle + 58.f);
				else
					state->m_feet_yaw = math::normalize_yaw(angle - 58.f);

				break;
			}
		}
		else {
			switch (g::ragebot::m_missed_shots[idx] % 2) {
			case 1:

				if (FreestandSide[idx] == 1)
					state->m_feet_yaw = math::normalize_yaw(angle + 58.f);
				else
					state->m_feet_yaw = math::normalize_yaw(angle - 58.f);

				break;
			case 0:

				if (FreestandSide[idx] == 1)
					state->m_feet_yaw = math::normalize_yaw(angle - 58.f);
				else
					state->m_feet_yaw = math::normalize_yaw(angle + 58.f);

				break;
			}
		}
	}

	state->m_feet_yaw = math::normalize_yaw(state->m_feet_yaw);
}
