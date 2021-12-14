#pragma once
#include "../../includes.hpp"

class antiaim : public singleton<antiaim> {
public:
	void run(c_usercmd* cmd);
	void do_lag(bool& send);
	void m_ang_vec(const vec3_t& angles, vec3_t& forward);
private:
	float at_target();
	float corrected_tickbase();
	void predict_lby_update(float sampletime, c_usercmd* ucmd, bool& sendpacket);
	void clamp_angles(vec3_t& angles);
	float get_fov(vec3_t view_angle, vec3_t aim_angle);
	float freestanding();
	void manual_override();
};