#pragma once
#include "../../includes.hpp"
#include "../animations/animations.hpp"

class ShotSnapshot
{
public:

	c_base_player* entity;
	std::string hitbox_where_shot;
	std::string resolver;
	float time;
	float first_processed_time;
	bool weapon_fire, bullet_impact;
	int hitgroup_hit;
	int damage;
	int hitbox;
	animation* record;
	vec3_t eyeangles;
	vec3_t impact, start;
	int backtrack;
	matrix_t* pMat;
	std::string get_info();
};

extern std::vector<ShotSnapshot> shot_snapshots;

class ragebot : public singleton<ragebot> 
{
public:
	animation backup_anims[65];

	void BackupPlayer(animation*);
	void SetAnims(animation*);
	void RestorePlayer(animation*);

	vec3_t HeadScan(animation* backshoot, int& hitbox, float& best_damage, float min_dmg);
	vec3_t PrimaryScan(animation* anims, int& hitbox, float& simtime, float& best_damage, float min_dmg);

	std::vector<int> GetHitboxesToScan(c_base_player*);
	std::vector<vec3_t> GetMultipoints(c_base_player*, int, matrix_t[128]);

	vec3_t FullScan(animation* anims, int& hitbox, float& simtime, float& best_damage, float min_dmg);
	vec3_t GetPoint(c_base_player* pBaseEntity, int iHitbox, matrix_t BoneMatrix[128]);

	int GetTicksToShoot();
	int GetTicksToStop();
	void FastStop();

	vec3_t GetAimVector(c_base_player*, float&, vec3_t&, animation*&, int&);
	bool Hitchance(vec3_t, bool, animation*, int&);
	bool IsAbleToShoot(float time);
	void DropTarget();

	int target_index = -1;
	float best_distance;
	bool did_dt;
	bool aimbotted_in_current_tick;
	bool fired_in_that_tick;
	float current_aim_simulationtime;
	int current_minusticks;

	vec3_t current_aim_position;
	bool lby_backtrack;
	vec3_t current_aim_player_origin;
	bool shot;
	bool Shooting;
	vec3_t Target;
	bool hitchanced;
	bool fired;
	vec3_t Angles;
	void Run();

	bool shoot_next_tick;
	bool last_tick_shooted;
	bool target_lethal;
	bool Shooted[65];
	bool Hitted[65];
	matrix_t BoneMatrix[128];
	int GetCurrentPriorityHitbox(c_base_player* pEntity);

	bool HeadAiming;
	animation* target_anims;

	vec3_t last_shot_angle;
	float LerpTime();

	clock_t last_shot_tick;

	void update_config();
	bool spread_limit();
	bool can_hit(vec3_t start, vec3_t end, animation* record, int box, bool in_shot, matrix_t* bones);
	bool valid_hitgroup(int index);

	void process_misses();
	bool CanHitHitbox(const vec3_t start, const vec3_t end, animation* _animation, studio_hdr_t* hdr, int box);
	struct
	{
		float damage;
		float hitchance;

		bool hitchance_consider_hitbox;
		bool delay_shot;
		bool unlag;
		float accuracy_boost;

		bool spread_limit;
		bool baim_air;
		bool baim_lethal;
		bool adaptive;
		float baim_hp_lower;

		struct
		{
			bool head;
			bool neck;
			bool upper_chest;
			bool chest;
			bool stomach;
			bool pelvis;
			bool arms;
			bool legs;
			bool feet;

		} m_hitboxes;

		float head_scale;
		float body_scale;
	} cfg;
private:
};