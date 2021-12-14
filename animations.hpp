#pragma once
#include "../../includes.hpp"
#include "../../hooks/hooks.hpp"
#include <optional>

extern struct animation
{
	animation() = default;
	explicit animation(c_base_player* player);
	explicit animation(c_base_player* player, vec3_t last_reliable_angle);
	void restore(c_base_player* player) const;
	void apply(c_base_player* player) const;
	void build_server_bones(c_base_player* player);
	bool is_valid(float range, float max_unlag);
	bool is_valid_extended();

	c_base_player* player;
	int32_t index;

	bool valid, has_anim_state;
	alignas(16) matrix_t bones[128];

	bool dormant;
	vec3_t velocity;
	vec3_t origin;
	matrix_t* bone_cache;
	vec3_t abs_origin;
	vec3_t obb_mins;
	vec3_t obb_maxs;
	animlayer_t layers[13];
	std::array<float, 24> poses;
	animstate_t* anim_state;
	float anim_time;
	float sim_time;
	float interp_time;
	float duck;
	float lby;
	float last_shot_time;
	vec3_t last_reliable_angle;
	vec3_t eye_angles;
	vec3_t abs_ang;
	int flags;
	int eflags;
	int effects;
	float m_flFeetCycle;
	float m_flFeetYawRate;
	int lag;
	bool didshot;
	std::string resolver;
};

struct globals_data
{
	float flCurtime;
	float flFrametime;
	float flAbsFrametime;
	int iFramecount;
	int iTickscount;
	float flInterpAmount;
};

struct entity_data
{
	float m_flFeetYawRate;
	float m_flFeetYawCycle;
	float m_flLowerBodyYaw;
	float m_flEyeYaw;
	float m_flDuckamount;
	int m_iFlags;
	int m_iEFlags;
	animlayer_t Layers[15];
	bool m_bValidData = false;
};

class animations : public singleton<animations> {

private:

	struct animation_info {
		animation_info(c_base_player* player, std::deque<animation> animations)
			: player(player), frames(std::move(animations)), last_spawn_time(0)
		{
		}

		void m_update_player(c_base_player* player);
		void update_animations(animation* to, animation* from);

		c_base_player* player;
		std::deque<animation> frames;

		// last time this player spawned
		float last_spawn_time;
		float goal_feet_yaw;
		vec3_t last_reliable_angle;
	};

	std::unordered_map<unsigned long, animation_info> animation_infos;

public:

	animstate_t* m_nState;
	matrix_t fake_matrix[128];

	void manage_local_animations();
	void manage_fake_animations();
	void update_players();

public:

	animation_info* get_animation_info(c_base_player* player);
	std::optional<animation*> get_latest_animation(c_base_player* player);
	std::optional<animation*> get_oldest_animation(c_base_player* player);
	std::optional<std::pair<animation*, animation*>> get_intermediate_animations(c_base_player* player, float range = 1.f);
	std::vector<animation*> get_valid_animations(c_base_player* player, float range = 1.f);
	std::optional<animation*> get_latest_firing_animation(c_base_player* player);

public:

	entity_data m_Data[65];
	void save_data(c_base_player* pEnt, entity_data* data);
	void restore_ent_data(c_base_player* pEnt, entity_data* data);
	void save_globals(globals_data* data);
	void restore_globals(globals_data* data);
	void update_globals(c_base_player* pEnt);
	void main(c_base_player* pEnt);
	void update(c_base_player* pEnt, float m_flSimulationTime);
	void on_create();

};