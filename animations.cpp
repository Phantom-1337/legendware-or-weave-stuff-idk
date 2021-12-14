#include "animations.hpp"
#include "../rage/autowall.hpp"
#include "../rage/resolver.hpp"

float calculate_lerp()
{
	static auto cl_interp = i::cvar->find_var(XorStr("cl_interp"));
	static auto cl_updaterate = i::cvar->find_var(XorStr("cl_updaterate"));
	const auto update_rate = cl_updaterate->get_int();
	const auto interp_ratio = cl_interp->get_float();

	auto lerp = interp_ratio / update_rate;

	if (lerp <= interp_ratio)
		lerp = interp_ratio;

	return lerp;
}

void extrapolate(c_base_player* player, vec3_t& origin, vec3_t& velocity, int& flags, bool on_ground)
{
	static const auto sv_gravity = i::cvar->find_var(XorStr("sv_gravity"));
	static const auto sv_jump_impulse = i::cvar->find_var(XorStr("sv_jump_impulse"));

	if (!(flags & fl_onground))
		velocity.z -= ticks2time(sv_gravity->get_float());

	else if (player->m_fFlags() & fl_onground && !on_ground)
		velocity.z = sv_jump_impulse->get_float();

	auto src = origin;
	auto end = src + velocity * i::globalvars->m_interval_per_tick;

	ray_t r;
	r.initialize(src, end, player->m_vecMaxs(), player->m_vecMaxs());

	trace_t t;
	trace_filter filter;
	filter.skip = player;

	i::trace->trace_ray(r, mask_playersolid, &filter, &t);

	if (t.fraction != 1.f)
	{
		for (auto i = 0; i < 2; i++)
		{
			velocity -= t.plane.normal * velocity.dot_product(t.plane.normal);

			const auto dot = velocity.dot_product(t.plane.normal);
			if (dot < 0.f)
				velocity -= vec3_t(dot * t.plane.normal.x,
					dot * t.plane.normal.y, dot * t.plane.normal.z);

			end = t.end + velocity * ticks2time(1.f - t.fraction);

			r.initialize(t.end, end, player->m_vecMaxs(), player->m_vecMaxs());
			i::trace->trace_ray(r, mask_playersolid, &filter, &t);

			if (t.fraction == 1.f)
				break;
		}
	}

	origin = end = t.end;
	end.z -= 2.f;

	r.initialize(origin, end, player->m_vecMaxs(), player->m_vecMaxs());
	i::trace->trace_ray(r, mask_playersolid, &filter, &t);

	flags &= ~fl_onground;

	if (t.did_hit() && t.plane.normal.z > .7f)
		flags |= fl_onground;
}

bool animation::is_valid(float range = .2f, float max_unlag = .2f)
{
	if (!i::clientstate->m_net_channel_info || !valid)
		return false;

	const auto correct = std::clamp(i::clientstate->m_net_channel_info->get_latency(flow_incoming) + i::clientstate->m_net_channel_info->get_latency(flow_outgoing)
		+ calculate_lerp(), 0.f, max_unlag);

	return fabsf(correct - (i::globalvars->m_cur_time - sim_time)) < range && correct < 1.f;
}

animation::animation(c_base_player* player)
{
	const auto weapon = player->get_active_weapon();

	this->player = player;
	index = player->ent_index();
	dormant = player->is_dormant();
	velocity = player->m_vecVelocity();
	origin = player->m_vecOrigin();
	abs_origin = player->get_abs_origin();
	obb_mins = player->m_vecMins();
	obb_maxs = player->m_vecMaxs();
	memcpy(layers, player->get_animoverlays(), sizeof(animlayer_t) * 13);
	poses = player->m_flPoseParameter();
	anim_state = player->get_animstate();
	sim_time = player->m_flSimulationTime();
	interp_time = 0.f;
	last_shot_time = weapon ? weapon->m_fLastShotTime() : 0.f;
	duck = player->m_flDuckAmount();
	lby = player->m_flLowerBodyYawTarget();
	eye_angles = player->m_angEyeAngles();
	abs_ang = player->get_abs_angles();
	flags = player->m_fFlags();
	eflags = player->m_iEFlags();
	effects = player->m_fEffects();

	lag = time2ticks(player->m_flSimulationTime() - player->m_flOldSimulationTime());

	// animations are off when we enter pvs, we do not want to shoot yet.
	valid = lag >= 0 && lag <= 14;

	// clamp it so we don't interpolate too far : )
	lag = std::clamp(lag, 0, 14);
}

animation::animation(c_base_player* player, vec3_t last_reliable_angle) : animation(player)
{
	this->last_reliable_angle = last_reliable_angle;
}

void animation::restore(c_base_player* player) const
{
	player->m_vecVelocity() = velocity;
	player->m_fFlags() = flags;
	player->m_iEFlags() = eflags;
	player->m_flDuckAmount() = duck;
	memcpy(player->get_animoverlays(), layers, sizeof(animlayer_t) * 13);
	player->m_flLowerBodyYawTarget() = lby;
	player->m_vecOrigin() = origin;
	player->set_abs_origin(origin);
}

void animation::apply(c_base_player* player) const
{
	player->m_flPoseParameter() = poses;
	player->m_angEyeAngles() = eye_angles;
	player->m_vecVelocity() = player->m_vecAbsVelocity() = velocity;
	player->m_flLowerBodyYawTarget() = lby;
	player->m_flDuckAmount() = duck;
	player->m_fFlags() = flags;
	player->m_vecOrigin() = origin;
	player->set_abs_origin(origin);
}

void animation::build_server_bones(c_base_player* player)
{
	const auto backup_occlusion_flags = player->m_iOcclusionFlags();
	const auto backup_occlusion_framecount = player->m_iOcclusionFrameCount();

	player->m_iOcclusionFlags() = 0;
	player->m_iOcclusionFrameCount() = 0;

	player->get_bone_acessor()->m_readable = player->get_bone_acessor()->m_writeable = 0;

	player->invalidate_bone_cache();

	player->m_fEffects() |= 0x8;

	const auto backup_bone_array = player->get_bone_array_for_write();
	player->get_bone_array_for_write() = bones;

	g::force_bone = true;
	player->setup_bones(nullptr, -1, 0x7FF00, sim_time);
	g::force_bone = false;

	player->get_bone_array_for_write() = backup_bone_array;

	player->m_iOcclusionFlags() = backup_occlusion_flags;
	player->m_iOcclusionFrameCount() = backup_occlusion_framecount;

	player->m_fEffects() &= ~0x8;
}


void animations::animation_info::update_animations(animation* record, animation* from)
{
	if (!from)
	{
		// set velocity and layers.
		record->velocity = player->m_vecVelocity();

		// fix feet spin.
		record->anim_state->m_feet_yaw_rate = 0.f;

		// apply record.
		record->apply(player);

		// run update.
		return m_update_player(player);
	}

	const auto new_velocity = player->m_vecVelocity();

	// restore old record.
	memcpy(player->get_animoverlays(), from->layers, sizeof(animlayer_t) * 13);
	player->set_abs_origin(record->origin);
	player->set_abs_angles(from->abs_ang);
	player->m_vecVelocity() = from->velocity;

	// setup velocity.
	record->velocity = new_velocity;

	// setup extrapolation parameters.
	auto old_origin = from->origin;
	auto old_flags = from->flags;

	for (auto i = 0; i < record->lag; i++)
	{
		// move time forward.
		const auto time = from->sim_time + ticks2time(i + 1);
		const auto lerp = 1.f - (record->sim_time - time) / (record->sim_time - from->sim_time);

		// resolve player.
		if (record->lag - 1 == i)
		{
			player->m_vecVelocity() = new_velocity;
			player->m_fFlags() = record->flags;
		}
		else // compute velocity and flags.
		{
			extrapolate(player, old_origin, player->m_vecVelocity(), player->m_fFlags(), old_flags & fl_onground);
			old_flags = player->m_fFlags();
		}

		player->get_animstate()->m_feet_yaw_rate = 0.f;

		// backup simtime.
		const auto backup_simtime = player->m_flSimulationTime();

		// set new simtime.
		player->m_flSimulationTime() = time;

		// run update.
		m_update_player(player);

		// restore old simtime.
		player->m_flSimulationTime() = backup_simtime;
	}

	if (!record->dormant && !from->dormant)
		record->didshot = record->last_shot_time > from->sim_time && record->last_shot_time <= record->sim_time;
}

void animations::update_players()
{
	if (!i::engine->is_in_game() && !i::engine->is_connected())
		return;

	const auto local_index = i::engine->get_local_player();
	const auto local = static_cast<c_base_player*>(i::entitylist->get_client_entity(local_index));

	if (!local || !local->is_alive())
		return;

	// erase outdated entries
	for (auto it = animation_infos.begin(); it != animation_infos.end();) {
		auto player = reinterpret_cast<c_base_player*>(i::entitylist->get_client_entity_from_handle(it->first));

		if (!player || player != it->second.player || !player->is_alive() || !local)
		{
			if (player)
				player->m_bClientSideAnimation() = true;

			it = animation_infos.erase(it);
		}
		else
			it = next(it);
	}

	if (!local)
	{
		for (auto i = 1; i <= i::globalvars->m_max_clients; ++i) {
			const auto entity = reinterpret_cast<c_base_player*>(i::entitylist->get_client_entity(i));
			if (entity && entity->is_player())
				entity->m_bClientSideAnimation() = true;
		}
	}

	for (auto i = 1; i <= i::globalvars->m_max_clients; ++i) {
		const auto entity = reinterpret_cast<c_base_player*>(i::entitylist->get_client_entity(i));

		if (!entity || !entity->is_player())
			continue;

		if (!entity->is_alive() || entity->is_dormant())
			continue;

		if (entity->ent_index() == local->ent_index())
			continue;

		if (entity->ent_index() != local->ent_index() && entity->m_iTeamNum() == local->m_iTeamNum()) {
			g::m_call_client_update_enemy = entity->m_bClientSideAnimation() = true;
			continue;
		}

		if (animation_infos.find(entity->get_handle().to_int()) == animation_infos.end())
			animation_infos.insert_or_assign(entity->get_handle().to_int(), animation_info(entity, { }));
	}

	// run post update
	for (auto& info : animation_infos)
	{
		auto& _animation = info.second;
		const auto player = _animation.player;

		// erase frames out-of-range
		for (int i = 0; i < _animation.frames.size(); i++)
			if (!_animation.frames[i].is_valid(0.45f, 0.2f))
				_animation.frames.erase(_animation.frames.begin() + i);

		// call resolver
		resolver::get().resolve_angles(player);

		// have we already seen this update?
		if (player->m_flSimulationTime() == player->m_flOldSimulationTime())
			continue;

		// reset animstate
		if (_animation.last_spawn_time != player->m_flSpawnTime())
		{
			const auto state = player->get_animstate();
			if (state)
				player->reset_animation_state(state);

			_animation.last_spawn_time = player->m_flSpawnTime();
		}

		// grab weapon
		const auto weapon = player->get_active_weapon();

		auto backup = animation(player);
		backup.apply(player);

		// grab previous
		animation* previous = nullptr;

		if (!_animation.frames.empty() && !_animation.frames.front().dormant)
			previous = &_animation.frames.front();

		const auto shot = weapon && previous && weapon->m_fLastShotTime() > previous->sim_time
			&& weapon->m_fLastShotTime() <= player->m_flSimulationTime();

		if (!shot)
			info.second.last_reliable_angle = player->m_angEyeAngles();

		// store server record
		auto& record = _animation.frames.emplace_front(player, info.second.last_reliable_angle);

		// run full update
		_animation.update_animations(&record, previous);

		backup.restore(player);

		// use uninterpolated data to generate our bone matrix
		record.build_server_bones(player);
	}
}

void animations::animation_info::m_update_player(c_base_player* pEnt)
{
	static bool& invalidate_bone_cache = **reinterpret_cast<bool**>(utils::find_sig_ida("client.dll", "C6 05 ? ? ? ? ? 89 47 70") + 2);

	float curtime = i::globalvars->m_cur_time;
	float frametime = i::globalvars->m_frame_time;
	float realtime = i::globalvars->m_real_time;
	float abstime = i::globalvars->m_absolute_frame_time;
	float framecount = i::globalvars->m_frame_count;
	float interp = i::globalvars->m_interpolation_amount;

	float flSimulationTime = pEnt->m_flSimulationTime();
	int iNextSimulationTick = flSimulationTime / i::globalvars->m_interval_per_tick + 1;

	i::globalvars->m_cur_time = flSimulationTime;
	i::globalvars->m_real_time = flSimulationTime;
	i::globalvars->m_frame_time = i::globalvars->m_interval_per_tick;
	i::globalvars->m_absolute_frame_time = i::globalvars->m_interval_per_tick;
	i::globalvars->m_frame_count = iNextSimulationTick;
	i::globalvars->m_interpolation_amount = 0;

	pEnt->m_fEffects() &= ~0x1000;
	pEnt->m_vecAbsVelocity() = pEnt->m_vecVelocity();

	if (pEnt->get_animstate()->m_last_clientside_anim_framecount == i::globalvars->m_frame_count)
		pEnt->get_animstate()->m_last_clientside_anim_framecount = i::globalvars->m_frame_count - 1;

	const bool backup_invalidate_bone_cache = invalidate_bone_cache;

	g::m_call_client_update_enemy = true;
	pEnt->m_bClientSideAnimation() = true;

	pEnt->update_clientside_animation();

	pEnt->m_bClientSideAnimation() = false;
	g::m_call_client_update_enemy = false;

	pEnt->invalidate_physics_recursive(angles_changed);
	pEnt->invalidate_physics_recursive(animation_changed);
	pEnt->invalidate_physics_recursive(sequence_changed);

	invalidate_bone_cache = backup_invalidate_bone_cache;

	i::globalvars->m_cur_time = curtime;
	i::globalvars->m_real_time = realtime;
	i::globalvars->m_frame_time = frametime;
	i::globalvars->m_absolute_frame_time = abstime;
	i::globalvars->m_frame_count = framecount;
	i::globalvars->m_interpolation_amount = interp;
}




bool animation::is_valid_extended() //https://yougame.biz/threads/160320/ - thanks to yg, sugar crush!!
{
	if (!i::clientstate->m_net_channel_info || !valid)
		return false;


	auto nci = i::engine->get_net_channel_info();

	const auto correct = std::clamp(nci->get_latency(flow_incoming) //hmm what we have.
		+ nci->get_latency(flow_outgoing)
		+ calculate_lerp(), 0.f, 0.2f);

	float deltaTime = fabsf(correct - (i::globalvars->m_cur_time - sim_time));
	float ping = 0.2f;

	return deltaTime < ping&& deltaTime >= ping - .2f;
}

animations::animation_info* animations::get_animation_info(c_base_player* player)
{
	auto info = animation_infos.find(player->get_handle().to_int());

	if (info == animation_infos.end())
		return nullptr;

	return &info->second;
}

std::optional<animation*> animations::get_latest_animation(c_base_player* player)
{
	const auto info = animation_infos.find(player->get_handle().to_int());

	if (info == animation_infos.end() || info->second.frames.empty())
		return std::nullopt;

	for (auto it = info->second.frames.begin(); it != info->second.frames.end(); it = next(it)) {
		if ((*it).is_valid_extended()) {
			if (time2ticks(fabsf((*it).sim_time - player->m_flSimulationTime())) < 25)
				return &*it;
		}
	}

	return std::nullopt;
}

std::optional<animation*> animations::get_oldest_animation(c_base_player* player)
{
	const auto info = animation_infos.find(player->get_handle().to_int());

	if (info == animation_infos.end() || info->second.frames.empty())
		return std::nullopt;

	for (auto it = info->second.frames.rbegin(); it != info->second.frames.rend(); it = next(it)) {
		if ((*it).is_valid_extended()) {
			return &*it;
		}
	}

	return std::nullopt;
}

std::vector<animation*> animations::get_valid_animations(c_base_player* player, const float range)
{
	std::vector<animation*> result;

	const auto info = animation_infos.find(player->get_handle().to_int());

	if (info == animation_infos.end() || info->second.frames.empty())
		return result;

	result.reserve(static_cast<int>(std::ceil(range * .2f / i::globalvars->m_interval_per_tick)));

	for (auto it = info->second.frames.begin(); it != info->second.frames.end(); it = next(it))
		if ((*it).is_valid(range * .2f))
			result.push_back(&*it);

	return result;
}

std::optional<animation*> animations::get_latest_firing_animation(c_base_player* player)
{
	const auto info = animation_infos.find(player->get_handle().to_int());

	if (info == animation_infos.end() || info->second.frames.empty())
		return std::nullopt;

	for (auto it = info->second.frames.begin(); it != info->second.frames.end(); it = next(it))
		if ((*it).is_valid_extended() && (*it).didshot)
			return &*it;

	return std::nullopt;
}

void animations::save_data(c_base_player* pEnt, entity_data* data)
{
	data->m_flFeetYawCycle = pEnt->get_animstate()->m_feet_cycle;
	data->m_flFeetYawRate = pEnt->get_animstate()->m_feet_yaw_rate;
	data->m_flLowerBodyYaw = pEnt->m_flLowerBodyYawTarget();
	data->m_iEFlags = pEnt->m_iEFlags();
	data->m_iFlags = pEnt->m_fFlags();
	data->m_bValidData = true;
}

void animations::restore_ent_data(c_base_player* pEnt, entity_data* data)
{
	pEnt->get_animstate()->m_feet_cycle = data->m_flFeetYawCycle;
	pEnt->get_animstate()->m_feet_yaw_rate = data->m_flFeetYawRate;
	pEnt->m_flLowerBodyYawTarget() = data->m_flLowerBodyYaw;
	pEnt->m_iEFlags() = data->m_iEFlags;
	pEnt->m_fFlags() = data->m_iFlags;
	data->m_bValidData = false;
}

void animations::save_globals(globals_data* data)
{
	data->flCurtime = i::globalvars->m_cur_time;
	data->flFrametime = i::globalvars->m_frame_time;
	data->flAbsFrametime = i::globalvars->m_absolute_frame_time;
	data->iFramecount = i::globalvars->m_frame_count;
	data->flInterpAmount = i::globalvars->m_interpolation_amount;
}

void animations::restore_globals(globals_data* data)
{
	i::globalvars->m_cur_time = data->flCurtime;
	i::globalvars->m_frame_time = data->flFrametime;
	i::globalvars->m_absolute_frame_time = data->flAbsFrametime;
	i::globalvars->m_frame_count = data->iFramecount;
	i::globalvars->m_interpolation_amount = data->flInterpAmount;
}

void animations::update_globals(c_base_player* pEnt)
{
	float flSimulationTime = pEnt->m_flSimulationTime();
	int iNextSimulationTick = flSimulationTime / i::globalvars->m_interval_per_tick + 1;

	i::globalvars->m_cur_time = flSimulationTime;
	i::globalvars->m_real_time = flSimulationTime;
	i::globalvars->m_frame_time = i::globalvars->m_interval_per_tick;
	i::globalvars->m_absolute_frame_time = i::globalvars->m_interval_per_tick;
	i::globalvars->m_frame_count = iNextSimulationTick;
	i::globalvars->m_interpolation_amount = 0;
}

void animations::main(c_base_player* pEnt)
{
	auto nData = m_Data[pEnt->ent_index()];

	globals_data nGlobalsData = *reinterpret_cast<globals_data*>(i::memalloc->allocate(sizeof(globals_data)));
	entity_data nEntityData = *reinterpret_cast<entity_data*>(i::memalloc->allocate(sizeof(entity_data)));

	if (!nData.m_bValidData)
		return;

	save_globals(&nGlobalsData);
	save_data(pEnt, &nEntityData);

	update_globals(pEnt);
	restore_ent_data(pEnt, &nData);

	if (pEnt->m_fFlags() & fl_onground)
	{
		pEnt->get_animstate()->m_on_ground = true;
		pEnt->get_animstate()->m_hit_ground = false;
	}

	*(float*)(uintptr_t(pEnt->get_animstate()) + 0x110) = 0.0f;
	pEnt->get_animstate()->m_feet_yaw_rate = 0.0f;

	if (pEnt->get_animstate()->m_last_clientside_anim_framecount == i::globalvars->m_frame_count)
		pEnt->get_animstate()->m_last_clientside_anim_framecount = i::globalvars->m_frame_count - 1;

	update(pEnt, pEnt->m_flSimulationTime());

	restore_ent_data(pEnt, &nEntityData);
	restore_globals(&nGlobalsData);
}

void animations::update(c_base_player* pEnt, float flSimulationTime)
{
	float curtime = i::globalvars->m_cur_time;
	float frametime = i::globalvars->m_frame_time;
	float interp = i::globalvars->m_interpolation_amount;

	auto IEFlags = pEnt->m_iEFlags();

	float FeetCycle = pEnt->get_animstate()->m_feet_cycle;
	float FeetRate = pEnt->get_animstate()->m_feet_yaw_rate;

	i::globalvars->m_cur_time = flSimulationTime;
	i::globalvars->m_frame_time = i::globalvars->m_interval_per_tick;
	i::globalvars->m_interpolation_amount = 0.0f;

	pEnt->m_iEFlags() &= ~0x1000;
	pEnt->m_vecAbsVelocity() = pEnt->m_vecVelocity();
	pEnt->m_vecVelocity().z = 0.0f;

	if (pEnt->get_animstate()->m_last_clientside_anim_framecount == i::globalvars->m_frame_count)
		pEnt->get_animstate()->m_last_clientside_anim_framecount = i::globalvars->m_frame_count - 1;

	pEnt->get_animstate()->m_feet_cycle = pEnt->get_animstate()->m_feet_yaw_rate;
	pEnt->get_animstate()->m_feet_yaw_rate = 0.0f;

	g::m_call_client_update_enemy = true;
	pEnt->m_bClientSideAnimation() = true;

	pEnt->update_animation_state(pEnt->get_animstate(), pEnt->m_angEyeAngles());
	pEnt->set_abs_angles(vec3_t(0, pEnt->get_animstate()->m_feet_yaw, 0));

	pEnt->m_bClientSideAnimation() = false;
	g::m_call_client_update_enemy = false;

	pEnt->invalidate_physics_recursive(angles_changed);
	pEnt->invalidate_physics_recursive(animation_changed);
	pEnt->invalidate_physics_recursive(sequence_changed);

	i::globalvars->m_cur_time = curtime;
	i::globalvars->m_frame_time = frametime;
	i::globalvars->m_interpolation_amount = interp;

	pEnt->m_iEFlags() = IEFlags;
	pEnt->get_animstate()->m_feet_cycle = FeetCycle;
	pEnt->get_animstate()->m_feet_yaw_rate = FeetRate;
}

void animations::on_create()
{
	for (int i = 1; i <= i::globalvars->m_max_clients; i++)
	{
		c_base_player* pEnt = (c_base_player*)i::entitylist->get_client_entity(i);

		if (!pEnt || !pEnt->is_alive() || !pEnt->is_player() || pEnt->ent_index() == g_local->ent_index() || pEnt->m_iTeamNum() == g_local->m_iTeamNum())
			continue;

		main(pEnt);
	}
}

void animations::manage_fake_animations()
{
	static c_base_handle* selfhandle = nullptr;
	static float spawntime = g_local->m_flSpawnTime();

	auto alloc = m_nState == nullptr;
	auto change = !alloc && selfhandle != &g_local->get_handle();
	auto reset = !alloc && !change && g_local->m_flSpawnTime() != spawntime;

	if (change) {
		memset(&m_nState, 0, sizeof(m_nState));
		selfhandle = (c_base_handle*)&g_local->get_handle();
	}
	if (reset) {
		g_local->reset_animation_state(m_nState);
		spawntime = g_local->m_flSpawnTime();
	}

	if (alloc || change) {
		m_nState = reinterpret_cast<animstate_t*>(i::memalloc->allocate(sizeof(animstate_t)));

		if (m_nState)
			g_local->create_anim_state(m_nState);
	}

	if (m_nState->m_last_clientside_anim_framecount == i::globalvars->m_frame_count)
		m_nState->m_last_clientside_anim_framecount -= 1.f;

	g_local->m_fEffects() |= 0x8;

	animlayer_t backup_layers[13];
	if (g_local->m_flSimulationTime() != g_local->m_flOldSimulationTime())
	{
		std::memcpy(backup_layers, g_local->get_animoverlays(),
			(sizeof(animlayer_t) * g_local->num_overlays()));

		g_local->update_animation_state(m_nState, g::non_visual);
		g_local->set_abs_angles(vec3_t(0, m_nState->m_feet_yaw, 0));

		g::m_call_bone = true;
		g_local->setup_bones(fake_matrix, 128, 0x7FF00, g_local->m_flSimulationTime());
		g::m_call_bone = false;

		for (auto i = 0; i < 128; i++)
		{
			fake_matrix[i][0][3] -= g_local->get_render_origin().x;
			fake_matrix[i][1][3] -= g_local->get_render_origin().y;
			fake_matrix[i][2][3] -= g_local->get_render_origin().z;
		}

		std::memcpy(g_local->get_animoverlays(), backup_layers,
			(sizeof(animlayer_t) * g_local->num_overlays()));
	}

	g_local->m_fEffects() &= ~0x8;
}

void animations::manage_local_animations()
{
	auto animstate = g_local->get_animstate();
	if (!animstate)
		return;

	const auto backup_frametime = i::globalvars->m_frame_time;
	const auto backup_curtime = i::globalvars->m_cur_time;

	animstate->m_feet_yaw = g::visual.y;

	if (animstate->m_last_clientside_anim_framecount == i::globalvars->m_frame_count)
		animstate->m_last_clientside_anim_framecount -= 1.f;

	i::globalvars->m_frame_time = i::globalvars->m_interval_per_tick;
	i::globalvars->m_cur_time = g_local->m_flSimulationTime();

	g_local->m_iEFlags() &= ~0x1000;
	g_local->m_vecAbsVelocity() = g_local->m_vecVelocity();

	static float angle = animstate->m_feet_yaw;
	animstate->m_feet_yaw_rate = 0.f;

	animlayer_t backup_layers[13];
	if (g_local->m_flSimulationTime() != g_local->m_flOldSimulationTime())
	{
		std::memcpy(backup_layers, g_local->get_animoverlays(),
			(sizeof(animlayer_t) * g_local->num_overlays()));

		g::m_call_client_update = true;
		g_local->update_animation_state(animstate, g::non_visual);
		g_local->update_clientside_animation();
		g::m_call_client_update = false;

		angle = animstate->m_feet_yaw;

		std::memcpy(g_local->get_animoverlays(), backup_layers,
			(sizeof(animlayer_t) * g_local->num_overlays()));
	}
	animstate->m_feet_yaw = angle;

	i::globalvars->m_cur_time = backup_curtime;
	i::globalvars->m_frame_time = backup_frametime;
}