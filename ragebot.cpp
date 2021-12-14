#include "ragebot.hpp"
#include "../animations/animations.hpp"
#include "antiaim.hpp"
#include "autowall.hpp"
#include "tickbase.hpp"
#include "../misc/misc.hpp"

bool should_stop_slide;
std::vector<ShotSnapshot> shot_snapshots;

void vec_angles(const vec3_t& forward, vec3_t& angles)
{
	float tmp, yaw, pitch;

	if (forward.y == 0 && forward.x == 0)
	{
		yaw = 0;
		if (forward.z > 0)
			pitch = 270;
		else
			pitch = 90;
	}
	else
	{
		yaw = (atan2(forward.y, forward.x) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		tmp = sqrt(forward.x * forward.x + forward.y * forward.y);
		pitch = (atan2(-forward.z, tmp) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	angles.x = pitch;
	angles.y = yaw;
	angles.z = 0;
}

void ang_vec_mult(const vec3_t angles, vec3_t& forward, vec3_t& right, vec3_t& up)
{
	float angle;
	static float sp, sy, cp, cy;

	angle = angles.x * (M_PI / 180.f);
	sp = sin(angle);
	cp = cos(angle);

	angle = angles.y * (M_PI / 180.f);
	sy = sin(angle);
	cy = cos(angle);

	forward.x = cp * cy;
	forward.y = cp * sy;
	forward.z = -sp;

	static float sr, cr;

	angle = angles.z * (M_PI / 180.f);
	sr = sin(angle);
	cr = cos(angle);

	right.x = -1 * sr * sp * cy + -1 * cr * -sy;
	right.y = -1 * sr * sp * sy + -1 * cr * cy;
	right.z = -1 * sr * cp;

	up.x = cr * sp * cy + -sr * -sy;
	up.y = cr * sp * sy + -sr * cy;
	up.z = cr * cp;
}

VOID sinCos(float radians, PFLOAT sine, PFLOAT cosine)
{
	__asm
	{
		fld dword ptr[radians]
		fsincos
		mov edx, dword ptr[cosine]
		mov eax, dword ptr[sine]
		fstp dword ptr[edx]
		fstp dword ptr[eax]
	}
}

void AngleMatrix(const vec3_t& angles, matrix_t& matrix)
{
	float sr, sp, sy, cr, cp, cy;

	sinCos(math::deg2rad(angles.y), &sy, &cy);
	sinCos(math::deg2rad(angles.x), &sp, &cp);
	sinCos(math::deg2rad(angles.z), &sr, &cr);

	// matrix = (YAW * PITCH) * ROLL
	matrix[0][0] = cp * cy;
	matrix[1][0] = cp * sy;
	matrix[2][0] = -sp;

	float crcy = cr * cy;
	float crsy = cr * sy;
	float srcy = sr * cy;
	float srsy = sr * sy;
	matrix[0][1] = sp * srcy - crsy;
	matrix[1][1] = sp * srsy + crcy;
	matrix[2][1] = sr * cp;

	matrix[0][2] = (sp * crcy + srsy);
	matrix[1][2] = (sp * crsy - srcy);
	matrix[2][2] = cr * cp;

	matrix[0][3] = 0.0f;
	matrix[1][3] = 0.0f;
	matrix[2][3] = 0.0f;
}

bool intersect_line_with_bb(vec3_t& start, vec3_t& end, vec3_t& min, vec3_t& max)
{
	float d1, d2, f;
	auto start_solid = true;
	auto t1 = -1.0f, t2 = 1.0f;

	const float s[3] = { start.x, start.y, start.z };
	const float e[3] = { end.x, end.y, end.z };
	const float mi[3] = { min.x, min.y, min.z };
	const float ma[3] = { max.x, max.y, max.z };

	for (auto i = 0; i < 6; i++) {
		if (i >= 3) {
			const auto j = i - 3;

			d1 = s[j] - ma[j];
			d2 = d1 + e[j];
		}
		else {
			d1 = -s[i] + mi[i];
			d2 = d1 - e[i];
		}

		if (d1 > 0.0f && d2 > 0.0f)
			return false;

		if (d1 <= 0.0f && d2 <= 0.0f)
			continue;

		if (d1 > 0)
			start_solid = false;

		if (d1 > d2) {
			f = d1;
			if (f < 0.0f)
				f = 0.0f;

			f /= d1 - d2;
			if (f > t1)
				t1 = f;
		}
		else {
			f = d1 / (d1 - d2);
			if (f < t2)
				t2 = f;
		}
	}

	return start_solid || (t1 < t2&& t1 >= 0.0f);
}

void vector_i_transform(const vec3_t& in1, const matrix_t& in2, vec3_t& out)
{
	out.x = (in1.x - in2[0][3]) * in2[0][0] + (in1.y - in2[1][3]) * in2[1][0] + (in1.z - in2[2][3]) * in2[2][0];
	out.y = (in1.x - in2[0][3]) * in2[0][1] + (in1.y - in2[1][3]) * in2[1][1] + (in1.z - in2[2][3]) * in2[2][1];
	out.z = (in1.x - in2[0][3]) * in2[0][2] + (in1.y - in2[1][3]) * in2[1][2] + (in1.z - in2[2][3]) * in2[2][2];
}

void vector_i_rotate(vec3_t in1, matrix_t in2, vec3_t& out)
{
	out.x = in1.x * in2[0][0] + in1.y * in2[1][0] + in1.z * in2[2][0];
	out.y = in1.x * in2[0][1] + in1.y * in2[1][1] + in1.z * in2[2][1];
	out.z = in1.x * in2[0][2] + in1.y * in2[1][2] + in1.z * in2[2][2];
}

vec3_t vector_rotate(vec3_t& in1, matrix_t& in2)
{
	return vec3_t(in1.dot_(in2[0]), in1.dot_(in2[1]), in1.dot_(in2[2]));
}

vec3_t vector_rotate(vec3_t& in1, vec3_t& in2)
{
	matrix_t m;
	AngleMatrix(in2, m);
	return vector_rotate(in1, m);
}

std::vector<int> ragebot::GetHitboxesToScan(c_base_player* pEntity)
{
	std::vector< int > hitboxes;

	if (GetCurrentPriorityHitbox(pEntity) == (int)2)
	{
		if (cfg.m_hitboxes.chest)
			hitboxes.push_back((int)5);

		if (cfg.m_hitboxes.chest)
			hitboxes.push_back((int)3);

		if (cfg.m_hitboxes.pelvis)
			hitboxes.push_back((int)2);

		return hitboxes;
	}

	if (cfg.m_hitboxes.head)
		hitboxes.push_back((int)0);

	if (cfg.m_hitboxes.neck)
		hitboxes.push_back((int)1);

	if (cfg.m_hitboxes.upper_chest)
		hitboxes.push_back((int)6);

	if (cfg.m_hitboxes.chest)
		hitboxes.push_back((int)5);

	if (cfg.m_hitboxes.stomach)
		hitboxes.push_back((int)3);

	if (cfg.m_hitboxes.pelvis)
		hitboxes.push_back((int)2);

	if (cfg.m_hitboxes.arms)
	{
		hitboxes.push_back((int)14);
		hitboxes.push_back((int)13);
		hitboxes.push_back((int)18);
		hitboxes.push_back((int)16);
		hitboxes.push_back((int)17);
		hitboxes.push_back((int)15);
	}

	if (cfg.m_hitboxes.legs)
	{
		hitboxes.push_back((int)10);
		hitboxes.push_back((int)9);
	}

	if (cfg.m_hitboxes.feet) {
		hitboxes.push_back((int)12);
		hitboxes.push_back((int)11);
	}

	return hitboxes;
}

void ragebot::update_config()
{
	auto weapon = g_local->get_active_weapon();

	if (!weapon)
		return;

	auto id = weapon->m_iItemDefinitionIndex();
	auto type = weapon->get_cs_weapon_data()->weapon_type;

	int element = -1;

	if (type == weapontype_rifle || type == weapontype_submachinegun || type == weapontype_machinegun || type == weapontype_shotgun)
		element = 0;

	else if (type == weapontype_pistol && id != weapon_deagle && id != weapon_revolver)
		element = 1;

	else if (id == weapon_deagle || id == weapon_revolver)
		element = 2;

	else if (id == weapon_ssg08)
		element = 3;

	else if (id == weapon_awp)
		element = 4;

	else if (id == weapon_g3sg1 || id == weapon_scar20)
		element = 5;

	else
		element = 0;

	cfg.damage = g_vars.config.ragebot.aimbot.main.configurations.flt_custom_min_damage[element];
	cfg.hitchance = g_vars.config.ragebot.aimbot.main.configurations.flt_custom_hitchance[element];
	cfg.m_hitboxes.head = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[0][element];
	cfg.m_hitboxes.neck = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[1][element];
	cfg.m_hitboxes.upper_chest = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[2][element];
	cfg.m_hitboxes.chest = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[3][element];
	cfg.m_hitboxes.stomach = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[4][element];
	cfg.m_hitboxes.pelvis = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[5][element];
	cfg.m_hitboxes.arms = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[6][element];
	cfg.m_hitboxes.legs = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[7][element];
	cfg.m_hitboxes.feet = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.enabled[8][element];
	cfg.head_scale = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.head_scale[element];
	cfg.body_scale = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.body_scale[element];
	cfg.delay_shot = g_vars.config.ragebot.aimbot.main.other.delay_shot[element];
	cfg.unlag = g_vars.config.ragebot.aimbot.main.other.unlag_delay[element];
	cfg.accuracy_boost = g_vars.config.ragebot.aimbot.main.configurations.hitboxes.accuracy_boost[element];
	cfg.spread_limit = g_vars.config.ragebot.aimbot.main.other.spread_limit[element];
	cfg.baim_air = g_vars.config.ragebot.aimbot.main.other.baim_in_air[element];
	cfg.baim_lethal = g_vars.config.ragebot.aimbot.main.other.baim_in_air[element];
	cfg.adaptive = g_vars.config.ragebot.aimbot.main.other.baim_in_air[element];
	cfg.baim_hp_lower = g_vars.config.ragebot.aimbot.main.other.baim_hp_lower_flt[element];
	cfg.hitchance_consider_hitbox = g_vars.config.ragebot.aimbot.main.configurations.hitchance_consider[element];
}

void ragebot::DropTarget()
{
	target_index = -1;
	best_distance = INT_MAX;
	fired_in_that_tick = false;
	current_aim_position = vec3_t();
	shot = false;
	should_stop_slide = false;

	if (!tickbase::get().m_shift_data.m_did_shift_before && !tickbase::get().m_shift_data.m_should_be_ready)
		shoot_next_tick = false;

	autowall::get().reset();
}

std::vector<vec3_t> ragebot::GetMultipoints(c_base_player* pBaseEntity, int iHitbox, matrix_t BoneMatrix[128])
{
	std::vector<vec3_t> vPoints;

	if (!pBaseEntity)
		return vPoints;

	studio_hdr_t* pStudioModel = i::modelinfo->get_studio_model(pBaseEntity->get_model());
	studio_hitbox_set_t* set = pStudioModel->get_hitbox_set(0);

	if (!set)
		return vPoints;

	studio_box_t* untransformedBox = set->hitbox(iHitbox);
	if (!untransformedBox)
		return vPoints;

	vec3_t vecMin = { 0, 0, 0 };
	math::vector_transform(untransformedBox->mins, BoneMatrix[untransformedBox->bone], vecMin);

	vec3_t vecMax = { 0, 0, 0 };
	math::vector_transform(untransformedBox->maxs, BoneMatrix[untransformedBox->bone], vecMax);

	float mod = untransformedBox->radius != -1.f ? untransformedBox->radius : 0.f;
	vec3_t max;
	vec3_t min;

	float ps = 0.75f;
	if (pBaseEntity->m_vecVelocity().length() > 300.f && iHitbox > 0)
		ps = 0.f;
	else {
		if (iHitbox <= (int)1)
			ps = cfg.head_scale / 100.f;
		else if (iHitbox <= (int)7)
			ps = cfg.body_scale / 100.f;
	}

	math::vector_transform(untransformedBox->maxs += mod, BoneMatrix[untransformedBox->bone], max);
	math::vector_transform(untransformedBox->mins -= mod, BoneMatrix[untransformedBox->bone], min);

	auto center = (min + max) * 0.5f;
	if (ps <= 0.05f) {
		vPoints.push_back(center);
		return vPoints;
	}

	auto clamp_shit = [](float val, float min, float max) {
		if (val < min)
			return min;
		if (val > max)
			return max;
		return val;
	};
	vec3_t curAngles = math::calc_angle(center, g_local->get_eye_pos());
	vec3_t forward;
	antiaim::get().m_ang_vec(curAngles, forward);
	vec3_t right = forward.cross_product(vec3_t(0, 0, 1));
	vec3_t left = vec3_t(-right.x, -right.y, right.z);
	if (iHitbox == 0) {
		for (auto i = 0; i < 4; ++i)
			vPoints.push_back(center);
		vPoints[1].x += untransformedBox->radius * clamp_shit(0.f, ps - 0.2f, 0.87f); // near left ear
		vPoints[2].x -= untransformedBox->radius * clamp_shit(0.f, ps - 0.2f, 0.87f); // near right ear
		vPoints[3].z += untransformedBox->radius * ps - 0.05f; // forehead
	}
	else if (iHitbox == (int)1)
		vPoints.push_back(center);
	else if (iHitbox == (int)7 || iHitbox == (int)8 || iHitbox == (int)9 || iHitbox == (int)10 || iHitbox == (int)11 || iHitbox == (int)12) {

		if (iHitbox == (int)7 ||
			iHitbox == (int)8) {
			vPoints.push_back(center);
		}
		else if (iHitbox == (int)9 ||
			iHitbox == (int)10) {
			vPoints.push_back(center);
		}
		else if (iHitbox == (int)11 ||
			iHitbox == (int)12) {
			vPoints.push_back(center);
			vPoints[0].z += 5.f;
		}
	}
	else if (iHitbox == (int)13 ||
		iHitbox == (int)14 ||
		iHitbox == (int)15 ||
		iHitbox == (int)16 ||
		iHitbox == (int)17 ||
		iHitbox == (int)18)
	{
		vPoints.push_back(center);
	}
	else {
		for (auto i = 0; i < 3; ++i)
			vPoints.push_back(center);
		vPoints[1] += right * (untransformedBox->radius * ps);
		vPoints[2] += left * (untransformedBox->radius * ps);
	}

	return vPoints;
}

void ragebot::BackupPlayer(animation* anims)
{
	auto i = anims->player->ent_index();
	backup_anims[i].origin = anims->player->m_vecOrigin();
	backup_anims[i].abs_origin = anims->player->get_abs_origin();
	backup_anims[i].obb_mins = anims->player->m_vecMaxs();
	backup_anims[i].obb_maxs = anims->player->m_vecMaxs();
	backup_anims[i].bone_cache = anims->player->get_bone_cache()->m_cached_bones;
}

void ragebot::SetAnims(animation* anims)
{
	anims->player->m_vecOrigin() = anims->origin;
	anims->player->set_abs_origin(anims->abs_origin);
	anims->player->m_vecMaxs() = anims->obb_mins;
	anims->player->m_vecMaxs() = anims->obb_maxs;
	anims->player->get_bone_cache()->m_cached_bones = anims->bones;
}

void ragebot::RestorePlayer(animation* anims)
{
	auto i = anims->player->ent_index();
	anims->player->m_vecOrigin() = backup_anims[i].origin;
	anims->player->set_abs_origin(backup_anims[i].abs_origin);
	anims->player->m_vecMaxs() = backup_anims[i].obb_mins;
	anims->player->m_vecMaxs() = backup_anims[i].obb_maxs;
	anims->player->get_bone_cache()->m_cached_bones = backup_anims[i].bone_cache;
}

vec3_t ragebot::GetPoint(c_base_player* pBaseEntity, int iHitbox, matrix_t BoneMatrix[128])
{
	std::vector<vec3_t> vPoints;

	if (!pBaseEntity)
		return vec3_t(0, 0, 0);

	studio_hdr_t* pStudioModel = i::modelinfo->get_studio_model(pBaseEntity->get_model());
	studio_hitbox_set_t* set = pStudioModel->get_hitbox_set(0);

	if (!set)
		return vec3_t(0, 0, 0);

	studio_box_t* untransformedBox = set->hitbox(iHitbox);
	if (!untransformedBox)
		return vec3_t(0, 0, 0);

	vec3_t vecMin = { 0, 0, 0 };
	math::vector_transform(untransformedBox->mins, BoneMatrix[untransformedBox->bone], vecMin);

	vec3_t vecMax = { 0, 0, 0 };
	math::vector_transform(untransformedBox->maxs, BoneMatrix[untransformedBox->bone], vecMax);

	float mod = untransformedBox->radius != -1.f ? untransformedBox->radius : 0.f;
	vec3_t max;
	vec3_t min;

	math::vector_transform(untransformedBox->maxs += mod, BoneMatrix[untransformedBox->bone], max);
	math::vector_transform(untransformedBox->mins -= mod, BoneMatrix[untransformedBox->bone], min);

	return (min + max) * 0.5f;
}

vec3_t ragebot::HeadScan(animation* anims, int& hitbox, float& best_damage, float min_dmg)
{
	vec3_t best_point = vec3_t(0, 0, 0);
	memcpy(BoneMatrix, anims->bones, sizeof(matrix_t[128]));
	SetAnims(anims);
	int health = anims->player->m_iHealth();

	if (min_dmg > health)
		min_dmg = health + 1;

	std::vector<vec3_t> Points = GetMultipoints(anims->player, 0, BoneMatrix);
	for (auto HitBox : Points) {

		bool is_safepoint = can_hit(g_local->get_eye_pos(), HitBox, anims, 0, true, BoneMatrix);

		if ((g_vars.config.ragebot.aimbot.main.other.safepoint && utils::keybindparser(g_vars.config.ragebot.aimbot.main.other.safepoint_bind) || g_vars.config.ragebot.aimbot.main.other.safepoint) && !is_safepoint)
			continue;

		auto info = autowall::get().Think(HitBox, anims->player);
		if (info.m_damage > min_dmg && info.m_damage > best_damage)
		{
			hitbox = 0;
			best_point = HitBox;
			best_damage = info.m_damage;
		}
	}

	RestorePlayer(anims);
	return best_point;
}

float ragebot::LerpTime()
{
	static auto cl_interp = i::cvar->find_var(XorStr("cl_interp"));
	static auto cl_updaterate = i::cvar->find_var(XorStr("cl_updaterate"));
	const auto update_rate = cl_updaterate->get_int();
	const auto interp_ratio = cl_interp->get_float();

	auto lerp = interp_ratio / update_rate;

	if (lerp <= interp_ratio)
		lerp = interp_ratio;

	return lerp;
};

vec3_t ragebot::PrimaryScan(animation* anims, int& hitbox, float& simtime, float& best_damage, float min_dmg)
{
	memcpy(BoneMatrix, anims->bones, sizeof(matrix_t[128]));
	simtime = anims->sim_time;
	SetAnims(anims);

	best_damage = -1;
	const auto damage = min_dmg;
	auto best_point = vec3_t(0, 0, 0);
	auto health = anims->player->m_iHealth();
	if (min_dmg > health)
		min_dmg = health + 1;
	auto priority_hitbox = GetCurrentPriorityHitbox(anims->player);

	static const std::vector<int> hitboxes = {
		(int)0,
		(int)5,
		(int)3,
		(int)2,
		(int)10,
		(int)9,
	};

	for (auto HitboxID : hitboxes)
	{
		auto point = GetPoint(anims->player, HitboxID, BoneMatrix);

		bool is_safepoint = can_hit(g_local->get_eye_pos(), point, anims, HitboxID, true, BoneMatrix);

		if ((g_vars.config.ragebot.aimbot.main.other.safepoint && utils::keybindparser(g_vars.config.ragebot.aimbot.main.other.safepoint_bind) || g_vars.config.ragebot.aimbot.main.other.safepoint) && !is_safepoint)
			continue;

		auto info = autowall::get().Think(point, anims->player);
		if ((info.m_damage > min_dmg && info.m_damage > best_damage) || info.m_damage > health)
		{
			hitbox = HitboxID;
			best_point = point;
			best_damage = info.m_damage;
		}
	}

	RestorePlayer(anims);
	return best_point;
}

vec3_t ragebot::FullScan(animation* anims, int& hitbox, float& simtime, float& best_damage, float min_dmg)
{
	memcpy(BoneMatrix, anims->bones, sizeof(matrix_t[128]));
	simtime = anims->sim_time;
	best_damage = -1;
	vec3_t best_point = vec3_t(0, 0, 0);
	SetAnims(anims);

	int priority_hitbox = GetCurrentPriorityHitbox(anims->player);
	int health = anims->player->m_iHealth();

	if (min_dmg > health)
		min_dmg = health + 1;

	auto hitboxes = GetHitboxesToScan(anims->player);

	static const std::vector<int> upper_hitboxes = {
		(int)0,
		(int)1,
		(int)6,
		(int)5,
	};

	static const std::vector<int> baim_hitboxes = {
		(int)5,
		(int)3,
		(int)2,
	};

	bool baim_if_lethal = cfg.baim_lethal;
	bool can_shift = g_vars.config.ragebot.aimbot.exploits.active && g_vars.config.ragebot.aimbot.exploits.type == 0 && utils::keybindparser(g_vars.config.ragebot.aimbot.exploits.bind);

	if (baim_if_lethal || cfg.adaptive) {
		for (auto HitboxID : baim_hitboxes) {
			std::vector<vec3_t> Points = GetMultipoints(anims->player, HitboxID, BoneMatrix);
			for (int k = 0; k < Points.size(); k++)
			{
				bool is_safepoint = can_hit(g_local->get_eye_pos(), Points[k], anims, HitboxID, true, BoneMatrix);

				if ((g_vars.config.ragebot.aimbot.main.other.safepoint && utils::keybindparser(g_vars.config.ragebot.aimbot.main.other.safepoint_bind) || g_vars.config.ragebot.aimbot.main.other.safepoint && g_vars.config.ragebot.aimbot.main.other.safepoint_bind.id == -1) && !is_safepoint)
					continue;

				auto info = autowall::get().Think(Points[k], anims->player);
				if ((info.m_damage > min_dmg && info.m_damage > best_damage))
				{
					hitbox = HitboxID;
					best_point = Points[k];
					best_damage = info.m_damage;
				}
			}
		}
		if (baim_if_lethal && best_damage > health + 2) {
			target_lethal = true;
			RestorePlayer(anims);
			return best_point;
		}
		if (best_damage > 0 && cfg.adaptive) {
			if (can_shift) {
				if (best_damage * 2.f > health) {
					target_lethal = true;
					RestorePlayer(anims);
					return best_point;
				}
			}
			else {
				if (best_damage < health)
					target_lethal = false;
				RestorePlayer(anims);
				return best_point;
			}

		}
	}

	for (auto HitboxID : hitboxes)
	{
		std::vector<vec3_t> Points = GetMultipoints(anims->player, HitboxID, BoneMatrix);
		for (int k = 0; k < Points.size(); k++)
		{
			bool is_safepoint = can_hit(g_local->get_eye_pos(), Points[k], anims, HitboxID, true, BoneMatrix);

			if ((g_vars.config.ragebot.aimbot.main.other.safepoint && utils::keybindparser(g_vars.config.ragebot.aimbot.main.other.safepoint_bind) || g_vars.config.ragebot.aimbot.main.other.safepoint) && !is_safepoint)
				continue;

			auto info = autowall::get().Think(Points[k], anims->player);
			if ((info.m_damage > min_dmg && info.m_damage > best_damage))
			{
				hitbox = HitboxID;
				best_point = Points[k];
				best_damage = info.m_damage;
			}
		}
	}

	if (best_damage > anims->player->m_iHealth() + 2)
		target_lethal = true;
	RestorePlayer(anims);
	return best_point;
}

vec3_t ragebot::GetAimVector(c_base_player* pTarget, float& simtime, vec3_t& origin, animation*& best_anims, int& hitbox)
{
	if (GetHitboxesToScan(pTarget).size() == 0)
		return vec3_t(0, 0, 0);

	float m_damage = cfg.damage;

	auto latest_animation = animations::get().get_latest_animation(pTarget);
	auto record = latest_animation;

	if (!record.has_value() || !record.value()->player)
		return vec3_t(0, 0, 0);

	BackupPlayer(record.value());

	if (!g_vars.config.ragebot.aimbot.main.backtracking) {
		float damage = -1.f;
		best_anims = record.value();

		return FullScan(record.value(), hitbox, simtime, damage, m_damage);
	}

	if (g_vars.config.ragebot.aimbot.main.on_shot && !(g_vars.config.ragebot.aimbot.main.other.baim && utils::keybindparser(g_vars.config.ragebot.aimbot.main.other.baim_bind) || g_vars.config.ragebot.aimbot.main.other.baim)) {
		record = animations::get().get_latest_firing_animation(pTarget);
		if (record.has_value() && record.value()->player) {
			float damage = -1.f;
			best_anims = record.value();
			simtime = record.value()->sim_time;
			vec3_t backshoot = HeadScan(record.value(), hitbox, damage, m_damage);
			if (backshoot != vec3_t(0, 0, 0))
				return backshoot;
		}

	}

	auto oldest_animation = animations::get().get_oldest_animation(pTarget);
	vec3_t latest_origin = vec3_t(0, 0, 0);
	float best_damage_0 = -1.f, best_damage_1 = -1.f;

	record = latest_animation;
	vec3_t full_0;
	if (record.has_value())
	{
		latest_origin = record.value()->origin;
		float damage = -1.f;
		full_0 = FullScan(record.value(), hitbox, simtime, damage, m_damage);
		if (full_0 != vec3_t(0, 0, 0))
		{
			best_damage_0 = damage;
			if (best_damage_0 > pTarget->m_iHealth())
			{
				best_anims = record.value();
				return full_0;
			}
		}
	}

	record = oldest_animation;
	vec3_t full_1;
	if (record.has_value())
	{
		float damage = -1.f;
		full_1 = FullScan(record.value(), hitbox, simtime, damage, m_damage);
		if (full_1 != vec3_t(0, 0, 0))
		{
			best_damage_1 = damage;
			if (best_damage_1 > pTarget->m_iHealth())
			{
				best_anims = record.value();
				return full_1;
			}
		}

	}

	if (best_damage_0 >= best_damage_1 && best_damage_0 >= 1.f)
	{
		record = latest_animation;
		best_anims = record.value();
		return full_0;
	}
	else if (best_damage_1 >= 1.f)
	{
		record = oldest_animation;
		best_anims = record.value();
		return full_1;
	}

	return vec3_t(0, 0, 0);
}

bool m_cock_revolver()
{
	static auto r8cock_flag = true;
	static auto r8cock_time = 0.0f;

	float REVOLVER_COCK_TIME = 0.2421875f;
	const int count_needed = floor(REVOLVER_COCK_TIME / i::globalvars->m_interval_per_tick);
	static int cocks_done = 0;

	if (!g_local->get_active_weapon() || g_local->get_active_weapon()->m_iItemDefinitionIndex() != 64 || g_local->get_active_weapon()->m_flNextPrimaryAttack() > i::globalvars->m_cur_time)
	{
		if (g_local->get_active_weapon() && g_local->get_active_weapon()->m_iItemDefinitionIndex() == 64)
			g::cmd->buttons &= ~in_attack;

		ragebot::get().shot = false;
		return false;
	}

	if (cocks_done < count_needed)
	{
		g::cmd->buttons |= in_attack;
		++cocks_done;
		return false;
	}
	else
	{
		g::cmd->buttons &= ~in_attack;
		cocks_done = 0;
		return true;
	}

	g::cmd->buttons |= in_attack;

	float curtime = g_local->m_nTickBase() * i::globalvars->m_interval_per_tick;
	static float next_shoot_time = 0.f;

	bool ret = false;

	if (fabsf(next_shoot_time - curtime) < 0.5)
		next_shoot_time = curtime + 0.2f - i::globalvars->m_interval_per_tick;

	if (next_shoot_time - curtime - i::globalvars->m_interval_per_tick <= 0.f)
	{
		next_shoot_time = curtime + 0.2f;
		ret = true;
	}

	return ret;
}

void ragebot::FastStop()
{
	auto wpn_info = g_local->get_active_weapon()->get_cs_weapon_data();

	if (!wpn_info)
		return;

	auto get_standing_accuracy = [&]() -> const float
	{
		const auto max_speed = g_local->get_active_weapon()->m_zoomLevel() > 0 ? wpn_info->max_speed_alt : wpn_info->max_speed;
		return max_speed / 3.f;
	};

	auto velocity = g_local->m_vecVelocity();
	float speed = velocity.length_2d();
	float max_speed = (g_local->get_active_weapon()->m_zoomLevel() == 0 ? wpn_info->max_speed : wpn_info->max_speed_alt) * 0.1f;

	if (speed > max_speed) {
		should_stop_slide = false;
	}
	else {
		should_stop_slide = true;
		return;
	}

	if (speed <= get_standing_accuracy())
		return;

	vec3_t direction;
	vec_angles(velocity, direction);
	direction.y = g::real_angle.y - direction.y;
	vec3_t forward;

	antiaim::get().m_ang_vec(direction, forward);
	vec3_t negated_direction = forward * -speed;
	g::cmd->move.x = negated_direction.x;
	g::cmd->move.y = negated_direction.y;
}

int ragebot::GetTicksToShoot()
{
	if (IsAbleToShoot(ticks2time(g_local->m_nTickBase())))
		return -1;

	auto flServerTime = (float)g_local->m_nTickBase() * i::globalvars->m_interval_per_tick;
	auto flNextPrimaryAttack = g_local->get_active_weapon()->m_flNextPrimaryAttack();

	return time2ticks(fabsf(flNextPrimaryAttack - flServerTime));
}

int ragebot::GetTicksToStop()
{
	static auto predict_velocity = [](vec3_t* velocity)
	{
		float speed = velocity->length_2d();
		static auto sv_friction = i::cvar->find_var(XorStr("sv_friction"));
		static auto sv_stopspeed = i::cvar->find_var(XorStr("sv_stopspeed"));

		if (speed >= 1.f)
		{
			float friction = sv_friction->get_float();
			float stop_speed = std::max< float >(speed, sv_stopspeed->get_float());
			float time = std::max< float >(i::globalvars->m_interval_per_tick, i::globalvars->m_frame_time);
			*velocity *= std::max< float >(0.f, speed - friction * stop_speed * time / speed);
		}
	};

	vec3_t vel = g_local->m_vecVelocity();
	int ticks_to_stop = 0;
	while (true)
	{
		if (vel.length_2d() < 1.f)
			break;

		predict_velocity(&vel);
		ticks_to_stop++;
	}

	return ticks_to_stop;
}

static std::vector<std::tuple<float, float, float>> precomputed_seeds = { };

typedef void(*RandomSeed_t)(UINT);
RandomSeed_t m_RandomSeed = 0;
void random_seed(uint32_t seed)
{
	if (m_RandomSeed == NULL)
		m_RandomSeed = (RandomSeed_t)GetProcAddress(GetModuleHandle(XorStr("vstdlib.dll")), XorStr("RandomSeed"));
	m_RandomSeed(seed);
}

typedef float(*RandomFloat_t)(float, float);
RandomFloat_t m_RandomFloat;
float random_float(float flLow, float flHigh)
{
	if (m_RandomFloat == NULL)
		m_RandomFloat = (RandomFloat_t)GetProcAddress(GetModuleHandle(XorStr("vstdlib.dll")), XorStr("RandomFloat"));

	return m_RandomFloat(flLow, flHigh);
}

static const int total_seeds = 255;

void build_seed_table()
{
	if (!precomputed_seeds.empty())
		return;

	for (auto i = 0; i < total_seeds; i++) {
		random_seed(i + 1);

		const auto pi_seed = random_float(0.f, M_PI * 2);

		precomputed_seeds.emplace_back(random_float(0.f, 1.f),
			sin(pi_seed), cos(pi_seed));
	}
}

bool HitTraces(animation* _animation, const vec3_t position, const float chance, int box)
{
	build_seed_table();

	float HITCHANCE_MAX = 100.f;
	const auto weapon = g_local->get_active_weapon();

	if (!weapon)
		return false;

	const auto info = weapon->get_cs_weapon_data();

	if (!info)
		return false;

	const auto studio_model = i::modelinfo->get_studio_model(_animation->player->get_model());

	if (!studio_model)
		return false;

	if (ragebot::get().shoot_next_tick)
		HITCHANCE_MAX += 25;

	// performance optimization.
	if ((g_local->get_eye_pos() - position).length_2d() > info->range)
		return false;

	// setup calculation parameters.
	const auto id = weapon->m_iItemDefinitionIndex();
	const auto round_acc = [](const float accuracy) { return roundf(accuracy * 1000.f) / 1000.f; };
	const auto crouched = g_local->m_fFlags() & fl_ducking;

	// calculate inaccuracy.
	const auto weapon_inaccuracy = weapon->get_inaccuracy();

	if (id == 64)
		return weapon_inaccuracy < (crouched ? .0020f : .0055f);

	// calculate start and angle.
	auto start = g_local->get_eye_pos();
	const auto aim_angle = math::calc_angle(start, position);
	vec3_t forward, right, up;
	ang_vec_mult(aim_angle, forward, right, up);

	// keep track of all traces that hit the enemy.
	auto current = 0;
	int awalls_hit = 0;

	// setup calculation parameters.
	vec3_t total_spread, spread_angle, end;
	float inaccuracy, spread_x, spread_y;
	std::tuple<float, float, float>* seed;

	// use look-up-table to find average hit probability.
	for (auto i = 0u; i < total_seeds; i++)  // NOLINT(modernize-loop-convert)
	{
		// get seed.
		seed = &precomputed_seeds[i];

		// calculate spread.
		inaccuracy = std::get<0>(*seed) * weapon_inaccuracy;
		spread_x = std::get<2>(*seed) * inaccuracy;
		spread_y = std::get<1>(*seed) * inaccuracy;
		total_spread = (forward + right * spread_x + up * spread_y).normalized();

		// calculate angle with spread applied.
		vec_angles(total_spread, spread_angle);

		// calculate end point of trace.
		antiaim::get().m_ang_vec(spread_angle, end);
		end = start + end.normalized() * info->range;

		// did we hit the hitbox?
		if (ragebot::get().cfg.hitchance_consider_hitbox && box != (int)hitbox_left_foot && box != (int)hitbox_right_foot)
		{
			if (ragebot::get().CanHitHitbox(start, end, _animation, studio_model, box))
			{
				current++;

				if (ragebot::get().cfg.accuracy_boost > 1.f && autowall::get().Think(position, _animation->player, box).m_damage > 1.f)
					awalls_hit++;
			}
		}
		else
		{
			trace_t tr;
			ray_t ray;

			ray.initialize(start, end);
			i::trace->clip_ray_to_entity(ray, mask_shot | CONTENTS_GRATE, _animation->player, &tr);

			if (auto ent = tr.player; ent)
			{
				if (ent == _animation->player)
				{
					current++;

					if (ragebot::get().cfg.accuracy_boost > 1.f && autowall::get().Think(position, _animation->player, box).m_damage > 1.f)
						awalls_hit++;
				}
			}
		}

		// abort if hitchance is already sufficent.
		if (((static_cast<float>(current) / static_cast<float>(total_seeds)) >= (chance / HITCHANCE_MAX)))
		{
			if (((static_cast<float>(awalls_hit) / static_cast<float>(total_seeds)) >= (ragebot::get().cfg.accuracy_boost / HITCHANCE_MAX)) || ragebot::get().cfg.accuracy_boost <= 1.f)
				return true;
		}

		// abort if we can no longer reach hitchance.
		if (static_cast<float>(current + total_seeds - i) / static_cast<float>(total_seeds) < chance)
			return false;
	}

	return static_cast<float>(current) / static_cast<float>(total_seeds) >= chance;
}

float hitchance()
{
	auto wpn = g_local->get_active_weapon();
	float hitchance = 101.f;

	if (!wpn)
		return 0.f;

	float inaccuracy = wpn->get_inaccuracy();
	if (inaccuracy == 0)
		inaccuracy = 0.0000001;

	inaccuracy = 1 / inaccuracy;
	hitchance = inaccuracy;
	return hitchance;
}

bool ragebot::spread_limit()
{
	auto inaccuracy = g_local->get_active_weapon()->get_inaccuracy();

	if (!inaccuracy)
		inaccuracy = 0.0000001f;

	inaccuracy = 1.0f / inaccuracy;

	auto chance = cfg.hitchance * 1.5f;

	if (!chance)
		chance = 150.0f;

	return inaccuracy > chance;
}

bool ragebot::Hitchance(vec3_t Aimpoint, bool backtrack, animation* best, int& hitbox)
{
	bool r8 = g_local->get_active_weapon()->m_iItemDefinitionIndex() == 64;

	if (r8)
		return hitchance() > cfg.hitchance * (1.7 * (1.f - r8));
	else
		return HitTraces(best, Aimpoint, cfg.hitchance / 100.f, hitbox);
}

bool ragebot::IsAbleToShoot(float mtime)
{
	auto time = mtime;

	if (!g_local || !g_local->get_active_weapon())
		return false;

	if (!g_local->get_active_weapon())
		return false;

	const auto info = g_local->get_active_weapon()->get_cs_weapon_data();

	if (!info)
		return false;

	const auto is_zeus = g_local->get_active_weapon()->is_zeus();
	const auto is_knife = !is_zeus && info->weapon_type == weapontype_knife;

	if (g_local->get_active_weapon()->m_iItemDefinitionIndex() == 49 || !g_local->get_active_weapon()->is_weapon())
		return false;

	if (g_local->get_active_weapon()->m_iClip1() < 1 && !is_knife)
		return false;

	if (g_local->get_active_weapon()->is_reloading())
		return false;

	if ((g_local->m_flNextAttack() > time) || g_local->get_active_weapon()->m_flNextPrimaryAttack() > time || g_local->get_active_weapon()->m_flNextSecondaryAttack() > time)
	{
		if (g_local->get_active_weapon()->m_iItemDefinitionIndex() != 64 && info->weapon_type == weapontype_pistol)
			g::cmd->buttons &= ~in_attack;

		return false;
	}

	return true;
}

void ragebot::Run()
{
	auto weapon = g_local->get_active_weapon();

	if (!weapon)
		return;

	update_config();

	if (!weapon->is_weapon())
		return;

	int curhitbox;
	animation* best_anims = nullptr;
	int hitbox = -1;

	float simtime = 0;
	vec3_t minus_origin = vec3_t(0, 0, 0);
	animation* anims = nullptr;
	int box;

	shot = false;
	should_stop_slide = false;

	bool in_air = !(g_local->m_fFlags() & fl_onground);
	bool cock_revolver = m_cock_revolver();

	bool is_able_to_shoot = IsAbleToShoot(ticks2time(g_local->m_nTickBase())) || (weapon->m_iItemDefinitionIndex() == 64 && cock_revolver);

	for (auto i = 1; i <= i::globalvars->m_max_clients; i++)
	{
		auto pEntity = reinterpret_cast<c_base_player*>(i::entitylist->get_client_entity(i));

		if (pEntity == nullptr)
			continue;

		if (pEntity->ent_index() == g_local->ent_index())
			continue;

		if (!pEntity->is_alive())
		{
			g::ragebot::m_missed_shots[pEntity->ent_index()] = 0;
			continue;
		}

		if (pEntity->m_iHealth() <= 0)
			continue;

		if (pEntity->m_iTeamNum() == g_local->m_iTeamNum())
			continue;

		if (pEntity->is_dormant())
			continue;

		if (pEntity->m_bGunGameImmunity())
			continue;

		if (pEntity->m_fFlags() & fl_frozen)
			continue;

		auto intervals = i::globalvars->m_interval_per_tick * 2.0f;
		auto unlag = fabs(pEntity->m_flSimulationTime() - pEntity->m_flOldSimulationTime()) < intervals;

		if (!unlag && cfg.unlag)
			continue;

		if (cfg.delay_shot)
			if (pEntity->m_flSimulationTime() == pEntity->m_flOldSimulationTime())
				continue;

		target_lethal = false;

		vec3_t aim_position = GetAimVector(pEntity, simtime, minus_origin, anims, box);

		if (!anims)
			continue;

		int health = pEntity->m_iHealth();
		if (best_distance > health

			&& anims->player == pEntity && aim_position != vec3_t(0, 0, 0))
		{
			best_distance = health;
			target_index = i;
			current_aim_position = aim_position;
			current_aim_simulationtime = simtime;
			current_aim_player_origin = minus_origin;
			best_anims = anims;
			hitbox = box;
			target_anims = best_anims;
		}
	}

	static int delay = 0;
	did_dt = false;

	if (hitbox != -1 && target_index != -1 && best_anims && current_aim_position != vec3_t(0, 0, 0))
	{
		if (g_vars.config.ragebot.aimbot.main.autoscope && weapon->get_cs_weapon_data()->weapon_type == weapontype_sniper_rifle && weapon->m_zoomLevel() == 0)
		{
			g::cmd->buttons |= in_attack2;
			return;
		}

		bool htchance = Hitchance(current_aim_position, false, best_anims, hitbox);
		should_stop_slide = false;
		auto wpn_info = weapon->get_cs_weapon_data();

		if (g_local->m_fFlags() & fl_onground && !c_misc::get().m_in_slowwalk) {

			bool should_stop = GetTicksToShoot() <= GetTicksToStop()
				|| (g_vars.config.ragebot.aimbot.main.autostop) && !is_able_to_shoot;

			if (should_stop && g_vars.config.ragebot.aimbot.main.autostop)
			{
				if (!should_stop_slide)
					FastStop();

				should_stop_slide = true;
			}
			else
				should_stop_slide = false;
		}

		bool need_to_delay;

		if (cfg.spread_limit)
			need_to_delay = spread_limit();
		else
			need_to_delay = true;

		bool conditions = !tickbase::get().m_shift_data.m_should_attempt_shift || ((!g_vars.config.ragebot.aimbot.exploits.wait_charge || g::ragebot::goal_shift == 14 || tickbase::get().m_shift_data.m_should_disable) && tickbase::get().m_shift_data.m_should_attempt_shift) || (g_vars.config.ragebot.aimbot.exploits.wait_charge && g::ragebot::goal_shift == 8 && tickbase::get().m_shift_data.m_should_attempt_shift && !(tickbase::get().m_shift_data.m_prepare_recharge || tickbase::get().m_shift_data.m_did_shift_before && !tickbase::get().m_shift_data.m_should_be_ready));
		if (conditions && need_to_delay && htchance && is_able_to_shoot)
		{
			if (!c_misc::get().m_in_duck)
				g::send_packet = true;

			g::cmd->buttons |= in_attack;
		}

		if (conditions && need_to_delay && htchance && is_able_to_shoot)
		{
			if (g::cmd->buttons & in_attack) {

				if (shoot_next_tick)
					shoot_next_tick = false;

				g::cmd->viewangles = math::calc_angle(g_local->get_eye_pos(), current_aim_position) - g_local->m_aimPunchAngle() * 2.f;
				g::cmd->tick_count = time2ticks(best_anims->sim_time + LerpTime());

				if (!shoot_next_tick && g::ragebot::goal_shift == 14 && tickbase::get().m_shift_data.m_should_attempt_shift && !(tickbase::get().m_shift_data.m_prepare_recharge || tickbase::get().m_shift_data.m_did_shift_before && !tickbase::get().m_shift_data.m_should_be_ready)) {
					shoot_next_tick = true;
				}

				last_shot_angle = g::cmd->viewangles;
				ShotSnapshot snapshot;
				snapshot.entity = best_anims->player;
				snapshot.hitbox_where_shot = XorStr("none");
				snapshot.resolver = "";
				snapshot.time = i::globalvars->m_interval_per_tick * g_local->m_nTickBase();
				snapshot.first_processed_time = 0.f;
				snapshot.bullet_impact = false;
				snapshot.weapon_fire = false;
				snapshot.damage = -1;
				snapshot.start = g_local->get_eye_pos();
				snapshot.hitgroup_hit = -1;
				snapshot.backtrack = time2ticks(fabsf(best_anims->player->m_flSimulationTime() - current_aim_simulationtime));
				snapshot.eyeangles = best_anims->player->m_angEyeAngles();
				snapshot.hitbox = hitbox;
				snapshot.record = best_anims;
				shot_snapshots.push_back(snapshot);
				shot = true;
				last_shot_tick = clock();
				last_tick_shooted = true;
			}
		}
	}

	if (is_able_to_shoot && g::cmd->buttons & in_attack)
		shot = true;
}

bool ragebot::valid_hitgroup(int index)
{
	if ((index >= hitgroup_head && index <= hitgroup_rightleg) || index == hitgroup_gear)
		return true;

	return false;
}

bool ragebot::CanHitHitbox(const vec3_t start, const vec3_t end, animation* _animation, studio_hdr_t* hdr, int box)
{
	studio_hdr_t* pStudioModel = i::modelinfo->get_studio_model(_animation->player->get_model());
	studio_hitbox_set_t* set = pStudioModel->get_hitbox_set(0);

	if (!set)
		return false;

	studio_box_t* studio_box = set->hitbox(box);
	if (!studio_box)
		return false;

	vec3_t min, max;

	const auto is_capsule = studio_box->radius != -1.f;

	if (is_capsule)
	{
		math::vector_transform(studio_box->mins, _animation->bones[studio_box->bone], min);
		math::vector_transform(studio_box->maxs, _animation->bones[studio_box->bone], max);
		const auto dist = math::segment_to_segment(start, end, min, max);

		if (dist < studio_box->radius)
			return true;
	}

	if (!is_capsule)
	{
		math::vector_transform(vector_rotate(studio_box->mins, studio_box->mins), _animation->bones[studio_box->bone], min);
		math::vector_transform(vector_rotate(studio_box->maxs, studio_box->angle), _animation->bones[studio_box->bone], max);

		vector_i_transform(start, _animation->bones[studio_box->bone], min);
		vector_i_rotate(end, _animation->bones[studio_box->bone], max);

		if (intersect_line_with_bb(min, max, studio_box->mins, studio_box->maxs))
			return true;
	}

	return false;
}

void ragebot::process_misses()
{
	if (shot_snapshots.size() == 0)
		return;

	auto& snapshot = shot_snapshots.front();
	if (fabs((g_local->m_nTickBase() * i::globalvars->m_interval_per_tick) - snapshot.time) > 1.f)
	{
		shot_snapshots.erase(shot_snapshots.begin());
		return;
	}

	if (snapshot.first_processed_time != -1.f) {
		if (snapshot.damage == -1 && snapshot.weapon_fire && snapshot.bullet_impact && snapshot.record->player) {
			bool spread = false;
			if (snapshot.record->player) {
				const auto studio_model = i::modelinfo->get_studio_model(snapshot.record->player->get_model());

				if (studio_model)
				{
					const auto angle = math::calc_angle(snapshot.start, snapshot.impact);
					vec3_t forward;
					antiaim::get().m_ang_vec(angle, forward);
					const auto end = snapshot.impact + forward * 2000.f;
					if (!CanHitHitbox(snapshot.start, end, snapshot.record, studio_model, snapshot.hitbox))
						spread = true;
				}
			}

			/*if ( spread )
				visuals->add_log( "Missed shot due to spread", col_t( 255, 255, 255 ) );
			else {
				visuals->add_log( "Missed shot due to resolver", col_t( 255, 255, 255 ) );
				g::ragebot::m_missed_shots[ snapshot.entity->ent_index( ) ]++;
			}*/

			shot_snapshots.erase(shot_snapshots.begin());
		}
	}
}

bool ragebot::can_hit(vec3_t start, vec3_t end, animation* record, int box, bool in_shot, matrix_t* bones)
{
	if (!record || !record->player)
		return false;

	const auto backup_origin = record->player->m_vecOrigin();
	const auto backup_abs_origin = record->player->get_abs_origin();
	const auto backup_abs_angles = record->player->get_abs_angles();
	const auto backup_obb_mins = record->player->m_vecMaxs();
	const auto backup_obb_maxs = record->player->m_vecMaxs();
	const auto backup_cache = record->player->get_bone_cache()->m_cached_bones;

	auto matrix = bones;

	if (in_shot)
		matrix = bones;

	if (!matrix)
		return false;

	const model_t* model = record->player->get_model();
	if (!model)
		return false;

	studio_hdr_t* hdr = i::modelinfo->get_studio_model(model);
	if (!hdr)
		return false;

	studio_hitbox_set_t* set = hdr->get_hitbox_set(0);
	if (!set)
		return false;

	studio_box_t* bbox = set->hitbox(box);
	if (!bbox)
		return false;

	vec3_t min, max;
	const auto IsCapsule = bbox->radius != -1.f;

	if (IsCapsule) {
		math::vector_transform(bbox->mins, matrix[bbox->bone], min);
		math::vector_transform(bbox->maxs, matrix[bbox->bone], max);
		const auto dist = math::segment_to_segment(start, end, min, max);

		if (dist < bbox->radius) {
			return true;
		}
	}
	else {
		trace_t tr;

		record->player->m_vecOrigin() = record->origin;
		record->player->set_abs_origin(record->abs_origin);
		record->player->set_abs_angles(record->abs_ang);
		record->player->m_vecMaxs() = record->obb_mins;
		record->player->m_vecMaxs() = record->obb_maxs;
		record->player->get_bone_cache()->m_cached_bones = matrix;

		i::trace->clip_ray_to_entity(ray_t(start, end), mask_shot, record->player, &tr);

		record->player->m_vecOrigin() = backup_origin;
		record->player->set_abs_origin(backup_abs_origin);
		record->player->set_abs_angles(backup_abs_angles);
		record->player->m_vecMaxs() = backup_obb_mins;
		record->player->m_vecMaxs() = backup_obb_maxs;
		record->player->get_bone_cache()->m_cached_bones = backup_cache;

		if (tr.player == record->player && valid_hitgroup(tr.hit_group))
			return true;
	}

	return false;
}

int ragebot::GetCurrentPriorityHitbox(c_base_player* pEntity)
{
	if (!pEntity->is_alive())
		return -1;

	bool can_baim_on_miss = g::ragebot::m_missed_shots[pEntity->ent_index()] > 2;

	if (can_baim_on_miss)
		return (int)2;

	if (cfg.baim_air && !(pEntity->m_fFlags() & fl_onground))
		return (int)2;

	if ((pEntity->m_iHealth() <= cfg.baim_hp_lower))
		return (int)2;

	if (g_vars.config.ragebot.aimbot.main.other.baim && utils::keybindparser(g_vars.config.ragebot.aimbot.main.other.baim_bind) || g_vars.config.ragebot.aimbot.main.other.baim)
		return (int)2;

	return 0;
}