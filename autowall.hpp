#pragma once
#include "../../includes.hpp"
#define Assert( _exp ) ((void)0)

struct ReturnInfo_t
{
	int m_damage;
	int m_hitgroup;
	int m_penetration_count;
	bool m_did_penetrate_wall;
	float m_thickness;
	vec3_t m_end;
	c_base_player* m_hit_entity;

	ReturnInfo_t(int damage, int hitgroup, int penetration_count, bool did_penetrate_wall, float thickness, c_base_player* hit_entity)
	{
		m_damage = damage;
		m_hitgroup = hitgroup;
		m_penetration_count = penetration_count;
		m_did_penetrate_wall = did_penetrate_wall;
		m_thickness = thickness;
		m_hit_entity = hit_entity;
	}
};

class autowall : public singleton<autowall>
{
private:

	struct FireBulletData_t
	{
		vec3_t m_start;
		vec3_t m_end;
		vec3_t m_current_position;
		vec3_t m_direction;

		trace_filter* m_filter;
		trace_t m_enter_trace;

		float m_thickness;
		float m_current_damage;
		int m_penetration_count;
	};

	void ScaleDamage(c_base_player* e, weapon_info_t* weapon_info, int hitgroup, float& current_damage);
	bool HandleBulletPenetration(weapon_info_t* info, FireBulletData_t& data, bool extracheck = false, vec3_t point = vec3_t(0, 0, 0));
	bool TraceToExit(trace_t* enter_trace, vec3_t start, vec3_t dir, trace_t* exit_trace);
	void TraceLine(vec3_t& start, vec3_t& end, unsigned int mask, c_base_player* ignore, trace_t* trace);
	void ClipTrace(vec3_t& start, vec3_t& end, c_base_player* e, unsigned int mask, i_trace_filter* filter, trace_t* old_trace);
	bool IsBreakableEntity(c_base_player* e);

	float HitgroupDamage(int iHitGroup);

public:
	std::vector<float> scanned_damage;
	std::vector<vec3_t> scanned_points;

	void reset()
	{
		scanned_damage.clear();
		scanned_points.clear();
	}

	bool CanHitFloatingPoint(const vec3_t& point, const vec3_t& source);
	ReturnInfo_t Think(vec3_t pos, c_base_player* target, int specific_hitgroup = -1);
};
