#include "tickbase.hpp"
#include "../misc/misc.hpp"

bool CanFireWeapon(c_base_combat_weapon* wp, float curtime)
{
	if (!wp)
		return false;

	bool can_fire = ticks2time(g_local->m_nTickBase()) >= g_local->m_nTickBase();

	if (!can_fire)
		return false;

	if (curtime >= wp->m_flNextPrimaryAttack())
		return true;

	return false;
}

bool IsFiring(c_base_combat_weapon* wp, float curtime)
{
	return g::cmd->buttons & in_attack && CanFireWeapon(wp, curtime);
}

void tickbase::PreMovement()
{
	m_shift_data.m_next_shift_amount = m_shift_data.m_ticks_to_shift = 0;
}

void tickbase::PostMovement()
{
	if (!g_vars.config.ragebot.aimbot.exploits.active)
		return;

	auto weapon = g_local->get_active_weapon();

	if (!weapon || !g::cmd) {
		return;
	}

	auto data = weapon->get_cs_weapon_data();

	if (!data) {
		return;
	}

	if (weapon->m_iItemDefinitionIndex() == weapon_revolver ||
		weapon->m_iItemDefinitionIndex() == weapon_c4 ||
		data->weapon_type == weapontype_knife ||
		data->weapon_type == weapontype_grenade)
	{
		m_shift_data.m_did_shift_before = false;
		m_shift_data.m_should_be_ready = false;
		m_shift_data.m_should_disable = true;
		return;
	}

	if (!m_shift_data.m_should_attempt_shift) {
		m_shift_data.m_did_shift_before = false;
		m_shift_data.m_should_be_ready = false;
		return;
	}

	m_shift_data.m_should_disable = false;

	bool bFastRecovery = false;
	float flNonShiftedTime = ticks2time(g_local->m_nTickBase() - g::ragebot::goal_shift);

	bool bCanShootNormally = CanFireWeapon(weapon, ticks2time(g_local->m_nTickBase()));
	bool bCanShootIn12Ticks = CanFireWeapon(weapon, flNonShiftedTime);
	bool bIsShooting = IsFiring(weapon, ticks2time(g_local->m_nTickBase()));

	m_shift_data.m_can_shift_tickbase = bCanShootIn12Ticks || (!bCanShootNormally || bFastRecovery) && (m_shift_data.m_did_shift_before);

	if (m_shift_data.m_can_shift_tickbase && !c_misc::get().m_in_duck) {

		m_shift_data.m_next_shift_amount = g::ragebot::goal_shift;
	}
	else {
		m_shift_data.m_next_shift_amount = 0;
		m_shift_data.m_should_be_ready = false;
	}

	if (m_shift_data.m_next_shift_amount > 0) {

		if (bCanShootIn12Ticks) {
			if (m_shift_data.m_prepare_recharge && !bIsShooting) {
				m_shift_data.m_needs_recharge = g::ragebot::goal_shift;
				m_shift_data.m_prepare_recharge = false;
			}
			else {
				if (bIsShooting) {

					m_prediction.m_shifted_command = g::cmd->command_number;
					m_prediction.m_shifted_ticks = abs(m_shift_data.m_current_shift);
					m_prediction.m_original_tickbase = g_local->m_nTickBase();
					m_shift_data.m_ticks_to_shift = m_shift_data.m_next_shift_amount;
				}
			}
		}
		else {
			m_shift_data.m_prepare_recharge = true;
			m_shift_data.m_should_be_ready = false;
		}
	}
	else {
		m_shift_data.m_prepare_recharge = true;
		m_shift_data.m_should_be_ready = false;
	}

	m_shift_data.m_did_shift_before = m_shift_data.m_next_shift_amount > 0;
}