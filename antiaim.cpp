#include "antiaim.hpp"
#include "autowall.hpp"
#include "ragebot.hpp"
#include "tickbase.hpp"
#include "../misc/misc.hpp"

void antiaim::m_ang_vec(const vec3_t& angles, vec3_t& forward)
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

float antiaim::at_target()
{
	int cur_tar = 0;
	float last_dist = 999999999999.0f;

	for (int i = 0; i < i::globalvars->m_max_clients; i++) {
		auto entity = static_cast<c_base_player*>(i::entitylist->get_client_entity(i));

		if (!entity || entity->ent_index() == g_local->ent_index())
			continue;

		if (!entity->is_player())
			continue;

		auto m_player = (c_base_player*)entity;
		if (!m_player->is_dormant() && m_player->is_alive() && m_player->m_iTeamNum() != g_local->m_iTeamNum()) {
			float cur_dist = (entity->m_vecOrigin() - g_local->m_vecOrigin()).length();

			if (!cur_tar || cur_dist < last_dist) {
				cur_tar = i;
				last_dist = cur_dist;
			}
		}
	}

	if (cur_tar) {
		auto entity = static_cast<c_base_player*>(i::entitylist->get_client_entity(cur_tar));
		if (!entity) {
			return g::cmd->viewangles.y;
		}

		vec3_t target_angle = math::calc_angle(g_local->m_vecOrigin(), entity->m_vecOrigin());
		return target_angle.y;
	}

	return g::cmd->viewangles.y;
}

static bool lby;
float antiaim::corrected_tickbase()
{
	c_usercmd* last_ucmd = nullptr;
	int corrected_tickbase = 0;

	corrected_tickbase = (!last_ucmd || last_ucmd->hasbeenpredicted) ? (float)g_local->m_nTickBase() : corrected_tickbase++;
	last_ucmd = g::cmd;
	float corrected_curtime = corrected_tickbase * i::globalvars->m_interval_per_tick;
	return corrected_curtime;

};

void antiaim::predict_lby_update(float sampletime, c_usercmd* ucmd, bool& sendpacket)
{
	lby = false;
	static float next_lby_update_time = 0;
	auto local = g_local;

	if (!(local->m_fFlags() & fl_onground))
		return;

	if (local->m_vecVelocity().length_2d() > 0.1f)
		next_lby_update_time = corrected_tickbase() + 0.22f;
	else if (next_lby_update_time - corrected_tickbase() <= 0.0f) {
		next_lby_update_time = corrected_tickbase() + 1.1f;
		lby = true;
		sendpacket = false;
	}
	else if (next_lby_update_time - corrected_tickbase() <= 1 * i::globalvars->m_interval_per_tick)
		sendpacket = true;
}

void antiaim::clamp_angles(vec3_t& angles)
{
	if (angles.x > 89.0f) angles.x = 89.0f;
	else if (angles.x < -89.0f) angles.x = -89.0f;

	if (angles.y > 180.0f) angles.y = 180.0f;
	else if (angles.y < -180.0f) angles.y = -180.0f;

	angles.z = 0;
}

float antiaim::get_fov(vec3_t view_angle, vec3_t aim_angle)
{
	const float MaxDegrees = 360.0f;
	vec3_t Delta(0, 0, 0);
	vec3_t Forward(0, 0, 0);
	vec3_t delta = aim_angle - view_angle;
	clamp_angles(delta);
	return sqrtf(powf(delta.x, 2.0f) + powf(delta.y, 2.0f));
}

float antiaim::freestanding()
{
	enum {
		back,
		right,
		left
	};

	vec3_t view_angles;
	i::engine->get_viewangles(view_angles);

	static constexpr int damage_tolerance = 30;

	std::vector< c_base_player* > enemies;

	auto get_target = [&]() -> c_base_player* {
		c_base_player* target = nullptr;
		float best_fov = 360.f;

		for (int id = 1; id <= i::globalvars->m_max_clients; id++) {
			auto e = static_cast<c_base_player*>(i::entitylist->get_client_entity(id));

			if (e && e->is_alive() && !e->is_dormant() && e->m_iTeamNum() != g_local->m_iTeamNum() && e->ent_index() != g_local->ent_index())
			{
				float fov = get_fov(vec3_t(view_angles.x, view_angles.y, view_angles.z), math::calc_angle(g_local->get_eye_pos(), e->m_vecOrigin()));

				if (fov < best_fov) {
					target = e;
					best_fov = fov;
				}

				enemies.push_back(e);
			}
		}

		return target;
	};

	c_base_player* e = get_target();

	if (!e)
		return view_angles.y + 180;

	auto calculate_damage = [&](vec3_t point) -> int {
		int damage = 0;
		for (auto& enemy : enemies)
			damage += autowall::get().Think(enemy->get_eye_pos(), enemy, 1).m_damage;

		return damage;
	};

	auto rotate_and_extend = [](vec3_t position, float yaw, float distance) -> vec3_t {
		vec3_t direction;
		antiaim::get().m_ang_vec(vec3_t(0, yaw, 0), direction);

		return position + (direction * distance);
	};

	vec3_t
		head_position = g_local->get_eye_pos(),
		at_target = math::calc_angle(g_local->m_vecOrigin(), e->m_vecOrigin());

	float angles[3] = {
		at_target.y + 180.f,
		at_target.y - 70.f,
		at_target.y + 70.f
	};

	vec3_t head_positions[3] = {
		rotate_and_extend(head_position, at_target.y + 180.f, 0.f),
		rotate_and_extend(head_position, at_target.y - 70.f, 17.f),
		rotate_and_extend(head_position, at_target.y + 70.f, 17.f)
	};

	int damages[3] = {
		calculate_damage(head_positions[back]),
		calculate_damage(head_positions[right]),
		calculate_damage(head_positions[left])
	};

	if (damages[right] > damage_tolerance && damages[left] > damage_tolerance)
		return angles[back];

	if (at_target.x > 15.f)
		return angles[back];

	if (damages[right] == damages[left]) {
		auto trace_to_end = [](vec3_t start, vec3_t end) -> vec3_t {
			trace_t trace;
			trace_filter filter;
			ray_t ray;

			ray.initialize(start, end);
			i::trace->trace_ray(ray, mask_all, &filter, &trace);

			return trace.end;
		};

		vec3_t
			trace_right_endpos = trace_to_end(head_position, head_positions[right]),
			trace_left_endpos = trace_to_end(head_position, head_positions[left]);

		ray_t ray;
		trace_t trace;
		trace_filter filter;

		ray.initialize(trace_right_endpos, e->get_eye_pos());
		i::trace->trace_ray(ray, mask_all, &filter, &trace);
		float distance_1 = (trace.start - trace.end).length();

		ray.initialize(trace_left_endpos, e->get_eye_pos());
		i::trace->trace_ray(ray, mask_all, &filter, &trace);
		float distance_2 = (trace.start - trace.end).length();

		if (fabs(distance_1 - distance_2) > 15.f)
			return (distance_1 < distance_2) ? angles[right] : angles[left];
		else
			return angles[back];
	}
	else
		return (damages[right] < damages[left]) ? angles[right] : angles[left];
}

static bool left, right, back;
void antiaim::manual_override()
{
	if (utils::keybindparser(g_vars.config.ragebot.antiaim.main.keys.manuals.left))
	{
		left = true;
		right = false;
		back = false;
	}
	else if (utils::keybindparser(g_vars.config.ragebot.antiaim.main.keys.manuals.right))
	{
		left = false;
		right = true;
		back = false;
	}
	else if (utils::keybindparser(g_vars.config.ragebot.antiaim.main.keys.manuals.backward))
	{
		left = false;
		right = false;
		back = true;
	}
}

bool need_to_flip = false;
void antiaim::run(c_usercmd* cmd)
{
	auto wpn = g_local->get_active_weapon();
	bool is_manual = false;
	bool is_freestand = g_vars.config.ragebot.antiaim.main.freestand_override;

	if (!wpn)
		return;

	if (cmd->buttons & in_use)
		return;

	if (g_local->m_nMoveType() == 8 || g_local->m_nMoveType() == 9)
		return;

	if (wpn->get_cs_weapon_data()->weapon_type == weapontype_grenade)
		if (wpn->m_fThrowTime() > 0.0)
			return;

	if (wpn->m_iItemDefinitionIndex() == weapon_revolver)
	{
		if (ragebot::get().shot)
			return;
	}
	else
	{
		if (cmd->buttons & in_attack)
			return;

		if (wpn->get_cs_weapon_data()->weapon_type == weapontype_knife)
		{
			if ((cmd->buttons & in_attack || cmd->buttons & in_attack2) && ragebot::get().IsAbleToShoot(ticks2time(g_local->m_nTickBase())))
				return;
		}
	}

	if (g::send_packet)
		need_to_flip = !need_to_flip;

	switch (g_vars.config.ragebot.antiaim.main.pitch)
	{
	case 1:
		cmd->viewangles.x = -89.9f;
		break;
	case 2:
		cmd->viewangles.x = 89.9f;
		break;
	case 3:
		cmd->viewangles.x = 0.f;
		break;
	}

	if (g_vars.config.ragebot.antiaim.main.base_yaw == 1)
	{
		vec3_t angles;
		i::engine->get_viewangles(angles);
		cmd->viewangles.y = angles.y;
	}
	else if (g_vars.config.ragebot.antiaim.main.base_yaw == 2)
		cmd->viewangles.y = at_target();

	if (g_vars.config.ragebot.antiaim.main.freestand_override && !is_manual)
		cmd->viewangles.y = freestanding();

	if (!is_manual)
	{
		switch (g_vars.config.ragebot.antiaim.main.yaw)
		{
		case 1:
			cmd->viewangles.y += 180.f;
			break;
		case 2:
			cmd->viewangles.y = -90.f;
			break;
		}
	}

	need_to_flip ? cmd->viewangles.y -= g_vars.config.ragebot.antiaim.main.jitter : cmd->viewangles.y += g_vars.config.ragebot.antiaim.main.jitter;

	bool state = utils::keybindparser(g_vars.config.ragebot.antiaim.main.keys.swap);
	int m_rise = g_local->m_vecVelocity().length_2d() > 2.f ? 2 : 1;

	static float m_side;
	m_side = g_vars.config.ragebot.antiaim.main.jitter_around ? (state ? need_to_flip ? -1 : 1 : need_to_flip ? 1 : -1) : state ? 1 : -1;

	if (!is_manual)
		cmd->viewangles.y += state ? g_vars.config.ragebot.antiaim.main.in_body_lean : g_vars.config.ragebot.antiaim.main.body_lean;

	if (g_vars.config.ragebot.antiaim.main.fake == 3 && !g_vars.config.ragebot.antiaim.main.jitter_around)
	{
		static int old_fake;

		if (old_fake != m_side) {
			g::send_packet = true;
			old_fake = m_side;
		}
	}

	if (g_vars.config.ragebot.antiaim.main.fake == 1)
	{
		if (fabsf(cmd->move.y) < 5.0f) {
			if (cmd->buttons & in_duck)
				cmd->move.y = i::globalvars->m_tick_count & 1 ? 3.25f : -3.25f;
			else
				cmd->move.y = i::globalvars->m_tick_count & 1 ? 1.1f : -1.1f;
		}

		if (!g::send_packet)
			cmd->viewangles.y -= (g_vars.config.ragebot.antiaim.main.max_fake_delta * m_rise) * m_side;
	}
	else if (g_vars.config.ragebot.antiaim.main.fake == 2)
	{
		if (!g::send_packet)
			cmd->viewangles.y -= (g_vars.config.ragebot.antiaim.main.max_fake_delta * m_rise) * m_side;
	}
	else if (g_vars.config.ragebot.antiaim.main.fake == 3)
	{
		predict_lby_update(0.f, cmd, g::send_packet);

		if (!g::send_packet)
			cmd->viewangles.y -= 116.f * m_side;

		if (lby && g_local->m_vecVelocity().length_2d() < 10.f && !(cmd->buttons & in_jump) && g_local->m_fFlags() & fl_onground)
			cmd->viewangles.y -= 116.f * m_side;
	}
}

void antiaim::do_lag(bool& send)
{
	bool lag = false;
	int m_value = g_vars.config.ragebot.antiaim.fakelags.value;

	auto m_time_tick = ((int)(1.0f / i::globalvars->m_interval_per_tick)) / 64;
	bool active = g_vars.config.ragebot.antiaim.fakelags.sel[0] || g_vars.config.ragebot.antiaim.fakelags.sel[1] || g_vars.config.ragebot.antiaim.fakelags.sel[2] || g_vars.config.ragebot.antiaim.fakelags.sel[3];
	int m_choke;

	bool is_antiaim = g_vars.config.ragebot.antiaim.main.enable && g_vars.config.ragebot.antiaim.main.fake > 0;
	bool is_act_dt = g_vars.config.ragebot.aimbot.main.enable && g_vars.config.ragebot.aimbot.exploits.active && utils::keybindparser(g_vars.config.ragebot.aimbot.exploits.bind);
	bool is_dt = is_act_dt || tickbase::get().m_shift_data.m_should_attempt_shift && ((!tickbase::get().m_shift_data.m_should_be_ready && tickbase::get().m_shift_data.m_prepare_recharge) || tickbase::get().m_shift_data.m_needs_recharge || tickbase::get().m_shift_data.m_should_be_ready) && !c_misc::get().m_in_duck;

	auto wpn = g_local->get_active_weapon();
	if (!wpn)
		return;

	if (m_value > 0 && !c_misc::get().m_in_duck && active && g::cmd->buttons & in_attack) {
		send = true;
		return;
	}

	if (g_vars.config.ragebot.antiaim.fakelags.sel[0] && g_local->m_fFlags() & fl_onground && g_local->m_vecVelocity().length() < 2.f)
		lag = true;

	else if (g_vars.config.ragebot.antiaim.fakelags.sel[1] && g_local->m_fFlags() & fl_onground && g_local->m_vecVelocity().length() > 2.f && !c_misc::get().m_in_slowwalk)
		lag = true;

	else if (g_vars.config.ragebot.antiaim.fakelags.sel[2] && !(g_local->m_fFlags() & fl_onground))
		lag = true;

	else if (g_vars.config.ragebot.antiaim.fakelags.sel[3] && c_misc::get().m_in_slowwalk)
		lag = true;

	else if (c_misc::get().m_in_duck)
		lag = true;

	else if (is_dt && !c_misc::get().m_in_duck)
		lag = true;

	switch (g_vars.config.ragebot.antiaim.fakelags.type) {
	case 1:
		m_choke = m_value;
		break;
	case 2:
		m_choke = math::clamp(static_cast<int>(std::ceilf(69 / (g_local->m_vecVelocity().length() * m_time_tick))), m_value, 14);
		break;
	}

	if (lag)
	{
		if (!is_dt && !c_misc::get().m_in_duck)
		{
			if (m_value < 1)
			{
				if (is_antiaim)
					send = i::clientstate->m_net_channel->choked_packets >= 1;
			}
			else
				send = i::clientstate->m_net_channel->choked_packets >= m_choke;
		}

		else if (is_dt && !c_misc::get().m_in_duck)
			send = i::clientstate->m_net_channel->choked_packets >= 1;

		else if (c_misc::get().m_in_duck)
			send = i::clientstate->m_net_channel->choked_packets >= 14;
	}
	else
	{
		if (is_antiaim && !is_dt && !c_misc::get().m_in_duck)
			send = i::clientstate->m_net_channel->choked_packets >= 1;
	}
}