#include "autowall.hpp"

void ang_vec(const vec3_t& angles, vec3_t& forward)
{
	Assert(s_bMathlibInitialized);
	Assert(forward);

	float sp, sy, cp, cy;

	sy = sin(math::deg2rad(angles.y));
	cy = cos(math::deg2rad(angles.y));

	sp = sin(math::deg2rad(angles.x));
	cp = cos(math::deg2rad(angles.x));

	forward.x = cp * cy;
	forward.y = cp * sy;
	forward.z = -sp;
}

ReturnInfo_t autowall::Think(vec3_t pos, c_base_player* target, int specific_hitgroup)
{
	ReturnInfo_t return_info = ReturnInfo_t(-1, -1, 4, false, 0.f, nullptr);

	vec3_t start = g_local->get_eye_pos();

	FireBulletData_t fire_bullet_data;
	fire_bullet_data.m_start = start;
	fire_bullet_data.m_end = pos;
	fire_bullet_data.m_current_position = start;
	fire_bullet_data.m_thickness = 0.f;
	fire_bullet_data.m_penetration_count = 5;

	math::angle_vectors(math::calc_angle(start, pos), fire_bullet_data.m_direction);

	static const auto filter_simple = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint32_t>(
		(void*)utils::find_sig_ida("client.dll", "55 8B EC 83 E4 F0 83 EC 7C 56 52")) + 0x3d);

	uint32_t dwFilter[4] = { filter_simple,
		reinterpret_cast<uint32_t>((c_base_player*)g_local), 0, 0 };

	fire_bullet_data.m_filter = (trace_filter*)(dwFilter);

	auto weapon = g_local->get_active_weapon();
	if (!weapon) return return_info;

	auto weapon_info = weapon->get_cs_weapon_data();
	if (!weapon_info) return return_info;

	float range = min(weapon_info->range, (start - pos).length());

	pos = start + (fire_bullet_data.m_direction * range);
	fire_bullet_data.m_current_damage = weapon_info->dmg;

	for (int i = 0; i < scanned_points.size(); i++) {
		if (pos == scanned_points[i]) {
			return_info.m_damage = scanned_damage[i];
			return return_info;
		}
	}

	while (fire_bullet_data.m_current_damage > 0 && fire_bullet_data.m_penetration_count > 0)
	{
		return_info.m_penetration_count = fire_bullet_data.m_penetration_count;
		auto curr_pos = fire_bullet_data.m_current_position + (fire_bullet_data.m_direction * 40.f);

		TraceLine(fire_bullet_data.m_current_position, pos, mask_shot | CONTENTS_GRATE, g_local, &fire_bullet_data.m_enter_trace);
		ClipTrace(fire_bullet_data.m_current_position, curr_pos, target, mask_shot | CONTENTS_GRATE, fire_bullet_data.m_filter, &fire_bullet_data.m_enter_trace);

		const float distance_traced = (fire_bullet_data.m_enter_trace.end - start).length();
		fire_bullet_data.m_current_damage *= pow(weapon_info->range_modifier, (distance_traced / 500.f));

		if (fire_bullet_data.m_enter_trace.fraction == 1.f)
		{
			if (target && specific_hitgroup != -1)
			{
				ScaleDamage(target, weapon_info, specific_hitgroup, fire_bullet_data.m_current_damage);

				return_info.m_damage = fire_bullet_data.m_current_damage;
				return_info.m_hitgroup = specific_hitgroup;
				return_info.m_end = fire_bullet_data.m_enter_trace.end;
				return_info.m_hit_entity = target;
			}
			else
			{
				return_info.m_damage = fire_bullet_data.m_current_damage;
				return_info.m_hitgroup = -1;
				return_info.m_end = fire_bullet_data.m_enter_trace.end;
				return_info.m_hit_entity = nullptr;
			}

			break;
		}

		if (fire_bullet_data.m_enter_trace.hit_group > 0 && fire_bullet_data.m_enter_trace.hit_group <= 8)
		{
			if
				(
					(target && fire_bullet_data.m_enter_trace.player != target)
					||
					(reinterpret_cast<c_base_player*>(fire_bullet_data.m_enter_trace.player)->m_iTeamNum() == g_local->m_iTeamNum()))
			{
				return_info.m_damage = -1;
				return return_info;
			}

			if (specific_hitgroup != -1)
			{
				ScaleDamage(reinterpret_cast<c_base_player*>(fire_bullet_data.m_enter_trace.player), weapon_info, specific_hitgroup, fire_bullet_data.m_current_damage);
			}
			else
			{
				ScaleDamage(reinterpret_cast<c_base_player*>(fire_bullet_data.m_enter_trace.player), weapon_info, fire_bullet_data.m_enter_trace.hit_group, fire_bullet_data.m_current_damage);
			}

			return_info.m_damage = fire_bullet_data.m_current_damage;
			return_info.m_hitgroup = fire_bullet_data.m_enter_trace.hit_group;
			return_info.m_end = fire_bullet_data.m_enter_trace.end;
			return_info.m_hit_entity = (c_base_player*)fire_bullet_data.m_enter_trace.player;

			break;
		}

		if (!HandleBulletPenetration(weapon_info, fire_bullet_data))
			break;

		return_info.m_did_penetrate_wall = true;
	}

	scanned_damage.push_back(return_info.m_damage);
	scanned_points.push_back(pos);

	return_info.m_penetration_count = fire_bullet_data.m_penetration_count;

	return return_info;
}

float autowall::HitgroupDamage(int iHitGroup)
{
	switch (iHitGroup)
	{
	case hitgroup_generic:
		return 0.5f;
	case hitgroup_head:
		return 2.0f;
	case hitgroup_chest:
		return 0.5f;
	case hitgroup_stomach:
		return 0.75f;
	case hitgroup_leftarm:
		return 0.5f;
	case hitgroup_rightarm:
		return 0.5f;
	case hitgroup_leftleg:
		return 0.375f;
	case hitgroup_rightleg:
		return 0.375f;
	case hitgroup_gear:
		return 0.5f;
	default:
		return 1.0f;
	}

	return 1.0f;
}

void autowall::ScaleDamage(c_base_player* e, weapon_info_t* weapon_info, int hitgroup, float& current_damage)
{
	static auto mp_damage_scale_ct_head = i::cvar->find_var(XorStr("mp_damage_scale_ct_head"));
	static auto mp_damage_scale_t_head = i::cvar->find_var(XorStr("mp_damage_scale_t_head"));
	static auto mp_damage_scale_ct_body = i::cvar->find_var(XorStr("mp_damage_scale_ct_body"));
	static auto mp_damage_scale_t_body = i::cvar->find_var(XorStr("mp_damage_scale_t_body"));

	auto team = e->m_iTeamNum();
	auto head_scale = team == 2 ? mp_damage_scale_ct_head->get_float() : mp_damage_scale_t_head->get_float();
	auto body_scale = team == 2 ? mp_damage_scale_ct_body->get_float() : mp_damage_scale_t_body->get_float();

	auto armor_heavy = e->m_bHasHeavyArmor();
	auto armor_value = static_cast<float>(e->m_ArmorValue());

	if (armor_heavy) head_scale *= 0.5f;

	switch (hitgroup)
	{
	case hitgroup_head:
		current_damage = (current_damage * 4.f) * head_scale;
		break;
	case hitgroup_chest:
	case 8:
		current_damage *= body_scale;
		break;
	case hitgroup_stomach:
		current_damage = (current_damage * 1.25f) * body_scale;
		break;
	case hitgroup_leftarm:
	case hitgroup_rightarm:
		current_damage *= body_scale;
		break;
	case hitgroup_leftleg:
	case hitgroup_rightleg:
		current_damage = (current_damage * 0.75f) * body_scale;
		break;
	default:
		break;
	}

	static auto IsArmored = [](c_base_player* player, int hitgroup)
	{
		auto has_helmet = player->m_bHasHelmet();
		auto armor_value = static_cast<float>(player->m_ArmorValue());

		if (armor_value > 0.f)
		{
			switch (hitgroup)
			{
			case hitgroup_generic:
			case hitgroup_chest:
			case hitgroup_stomach:
			case hitgroup_leftarm:
			case hitgroup_rightarm:
			case 8:
				return true;
				break;
			case hitgroup_head:
				return has_helmet || (bool)player->m_bHasHeavyArmor();
				break;
			default:
				return (bool)player->m_bHasHeavyArmor();
				break;
			}
		}

		return false;
	};

	if (IsArmored(e, hitgroup))
	{
		auto armor_scale = 1.f;
		auto armor_ratio = (weapon_info->armor_ratio * 0.5f);
		auto armor_bonus_ratio = 0.5f;

		if (armor_heavy)
		{
			armor_ratio *= 0.2f;
			armor_bonus_ratio = 0.33f;
			armor_scale = 0.25f;
		}

		float new_damage = current_damage * armor_ratio;
		float estiminated_damage = (current_damage - (current_damage * armor_ratio)) * (armor_scale * armor_bonus_ratio);
		if (estiminated_damage > armor_value) new_damage = (current_damage - (armor_value / armor_bonus_ratio));

		current_damage = new_damage;
	}
}

bool autowall::HandleBulletPenetration(weapon_info_t* info, FireBulletData_t& data, bool extracheck, vec3_t point)
{
	trace_t trace_exit;
	surfacedata_t* enter_surface_data = i::surface_props->get_surface_data(data.m_enter_trace.surface.surfaceProps);
	int enter_material = enter_surface_data->game.material;

	float enter_surf_penetration_modifier = enter_surface_data->game.flPenetrationModifier;
	float final_damage_modifier = 0.18f;
	float compined_penetration_modifier = 0.f;
	bool solid_surf = ((data.m_enter_trace.contents >> 3) & CONTENTS_SOLID);
	bool light_surf = ((data.m_enter_trace.surface.flags >> 7) & surf_light);

	if
		(
			data.m_penetration_count <= 0
			|| (!data.m_penetration_count && !light_surf && !solid_surf && enter_material != char_tex_glass && enter_material != char_tex_grate)
			|| info->penetration <= 0.f
			|| !TraceToExit(&data.m_enter_trace, data.m_enter_trace.end, data.m_direction, &trace_exit)
			&& !(i::trace->get_point_contents(data.m_enter_trace.end, mask_shot_hull | CONTENTS_HITBOX, NULL) & (mask_shot_hull | CONTENTS_HITBOX))
			)
	{
		return false;
	}

	surfacedata_t* exit_surface_data = i::surface_props->get_surface_data(trace_exit.surface.surfaceProps);
	int exit_material = exit_surface_data->game.material;
	float exit_surf_penetration_modifier = exit_surface_data->game.flPenetrationModifier;

	static auto dmg_reduction_bullets = i::cvar->find_var("ff_damage_reduction_bullets")->get_float();
	static auto dmg_bullet_penetration = i::cvar->find_var("ff_damage_bullet_penetration")->get_float();

	auto ent =
		reinterpret_cast<c_base_player*>(data.m_enter_trace.player);

	if (enter_material == char_tex_grate || enter_material == char_tex_glass)
	{
		compined_penetration_modifier = 3.0f;
		final_damage_modifier = 0.05f;
	}
	else if (light_surf || solid_surf)
	{
		compined_penetration_modifier = 1.0f;
		final_damage_modifier = 0.16f;
	}
	else if (enter_material == char_tex_flesh && ent->m_iTeamNum() != g_local->m_iTeamNum() && !dmg_reduction_bullets)
	{
		if (!dmg_bullet_penetration)
			return false;

		compined_penetration_modifier = dmg_bullet_penetration;
		final_damage_modifier = 0.16f;
	}
	else
	{
		compined_penetration_modifier = (enter_surf_penetration_modifier + exit_surf_penetration_modifier) * 0.5f;
		final_damage_modifier = 0.16f;
	}

	if (enter_material == exit_material)
	{
		if (exit_material == char_tex_cardboard || exit_material == char_tex_wood)
			compined_penetration_modifier = 3.f;
		else if (exit_material == char_tex_plastic)
			compined_penetration_modifier = 2.0f;
	}

	float thickness = (trace_exit.end - data.m_enter_trace.end).length_sqr();
	float modifier = max(0.f, 1.f / compined_penetration_modifier);

	if (extracheck) {
		static auto VectortoVectorVisible = [&](vec3_t src, vec3_t point) -> bool {
			trace_t TraceInit;
			TraceLine(src, point, mask_solid, g_local, &TraceInit);
			trace_t Trace;
			TraceLine(src, point, mask_solid, (c_base_player*)TraceInit.player, &Trace);

			if (Trace.fraction == 1.0f || TraceInit.fraction == 1.0f)
				return true;

			return false;
		};

		if (!VectortoVectorVisible(trace_exit.end, point))
			return false;
	}

	float lost_damage = max(((modifier * thickness) / 24.f) + ((data.m_current_damage * final_damage_modifier) + (max(3.75f / info->penetration, 0.f) * 3.f * modifier)), 0.f);

	if (lost_damage > data.m_current_damage)
		return false;

	if (lost_damage > 0.f)
		data.m_current_damage -= lost_damage;

	if (data.m_current_damage < 1.f)
		return false;

	data.m_current_position = trace_exit.end;
	data.m_penetration_count--;

	return true;
}

bool autowall::TraceToExit(trace_t* enter_trace, vec3_t start, vec3_t dir, trace_t* exit_trace)
{
	vec3_t end;
	float distance = 0.f;
	signed int distance_check = 25;
	int first_contents = 0;

	do
	{
		distance += 3.5f;
		end = start + dir * distance;

		if (!first_contents) first_contents = i::trace->get_point_contents(end, mask_shot | CONTENTS_GRATE, NULL);

		int point_contents = i::trace->get_point_contents(end, mask_shot | CONTENTS_GRATE, NULL);

		if (!(point_contents & (mask_shot_hull | CONTENTS_HITBOX)) || point_contents & CONTENTS_HITBOX && point_contents != first_contents)
		{
			vec3_t new_end = end - (dir * 4.f);

			ray_t ray;
			ray.initialize(end, new_end);

			i::trace->trace_ray(ray, mask_shot | CONTENTS_GRATE, nullptr, exit_trace);

			if (exit_trace->start_solid && exit_trace->surface.flags & surf_hitbox)
			{
				TraceLine(end, start, mask_shot_hull | CONTENTS_HITBOX, (c_base_player*)exit_trace->player, exit_trace);

				if (exit_trace->did_hit() && !exit_trace->start_solid) return true;

				continue;
			}

			if (exit_trace->did_hit() && !exit_trace->start_solid)
			{
				if (enter_trace->surface.flags & surf_nodraw || !(exit_trace->surface.flags & surf_nodraw)) {
					if (exit_trace->plane.normal.dot_product(dir) <= 1.f)
						return true;

					continue;
				}

				if (IsBreakableEntity((c_base_player*)enter_trace->player)
					&& IsBreakableEntity((c_base_player*)exit_trace->player))
					return true;

				continue;
			}

			if (exit_trace->surface.flags & surf_nodraw)
			{
				if (IsBreakableEntity((c_base_player*)enter_trace->player)
					&& IsBreakableEntity((c_base_player*)exit_trace->player))
				{
					return true;
				}
				else if (!(enter_trace->surface.flags & surf_nodraw))
				{
					continue;
				}
			}

			if ((!enter_trace->player
				|| enter_trace->player->ent_index() == 0)
				&& (IsBreakableEntity((c_base_player*)enter_trace->player)))
			{
				exit_trace = enter_trace;
				exit_trace->end = start + dir;
				return true;
			}

			continue;
		}

		distance_check--;
	} while (distance_check);

	return false;
}

void autowall::TraceLine(vec3_t& start, vec3_t& end, unsigned int mask, c_base_player* ignore, trace_t* trace)
{
	ray_t ray;
	ray.initialize(start, end);

	trace_filter filter;
	filter.skip = ignore;

	i::trace->trace_ray(ray, mask, &filter, trace);
}

void autowall::ClipTrace(vec3_t& start, vec3_t& end, c_base_player* e, unsigned int mask, i_trace_filter* filter, trace_t* old_trace)
{
	if (!e)
		return;

	vec3_t mins = e->m_vecMaxs(), maxs = e->m_vecMaxs();

	vec3_t dir(end - start);
	dir.normalized();

	vec3_t center = (maxs + mins) / 2, pos(center + e->m_vecOrigin());

	vec3_t to = pos - start;
	float range_along = dir.dot_product(to);

	float range;

	if (range_along < 0.f)
	{
		range = -to.length();
	}
	else if (range_along > dir.length())
	{
		range = -(pos - end).length();
	}
	else
	{
		auto ray(pos - (dir * range_along + start));
		range = ray.length();
	}

	if (range <= 60.f) //55.f 
	{
		trace_t trace;

		ray_t ray;
		ray.initialize(start, end);

		i::trace->clip_ray_to_entity(ray, mask, e, &trace);

		if (old_trace->fraction > trace.fraction) *old_trace = trace;
	}
}

bool autowall::IsBreakableEntity(c_base_player* e)
{
	if (!e || !e->ent_index())
		return false;

	static auto is_breakable_fn = reinterpret_cast<bool(__thiscall*)(c_base_player*)>(
		utils::find_sig_ida("client.dll", "55 8B EC 51 56 8B F1 85 F6 74 68"));

	const auto result = is_breakable_fn(e);
	auto class_id = e->get_client_class()->class_id;
	if (!result && (class_id == 10 || class_id == 31 || class_id == 11))
		return true;

	return result;
}

bool autowall::CanHitFloatingPoint(const vec3_t& point, const vec3_t& source)
{

	static auto VectortoVectorVisible = [&](vec3_t src, vec3_t point) -> bool {
		trace_t TraceInit;
		TraceLine(src, point, mask_solid, g_local, &TraceInit);
		trace_t Trace;
		TraceLine(src, point, mask_solid, (c_base_player*)TraceInit.player, &Trace);

		if (Trace.fraction == 1.0f || TraceInit.fraction == 1.0f)
			return true;

		return false;
	};

	FireBulletData_t data;
	data.m_start = source;
	data.m_filter = new trace_filter();
	data.m_filter->skip = g_local;
	vec3_t angles = math::calc_angle(data.m_start, point);
	ang_vec(angles, data.m_direction);
	data.m_direction.normalize();

	data.m_penetration_count = 1;
	auto weaponData = g_local->get_active_weapon()->get_cs_weapon_data();

	if (!weaponData)
		return false;

	data.m_current_damage = (float)weaponData->dmg;
	vec3_t end = data.m_start + (data.m_direction * weaponData->range);
	TraceLine(data.m_start, end, mask_shot | CONTENTS_HITBOX, g_local, &data.m_enter_trace);

	if (VectortoVectorVisible(data.m_start, point) || HandleBulletPenetration(weaponData, data, true, point))
		return true;

	delete data.m_filter;
	return false;
}