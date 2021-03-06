#pragma once
#include "data/entity.h"
#include "ai.h"
#include "constants.h"
#include "game.h"
#if DEBUG_AI_CONTROL
#include <typeinfo>
#endif
#include "data/components.h"
#include "drone.h"
#include "entities.h"
#include "data/priority_queue.h"

namespace VI
{

struct PlayerControlAI;
struct Transform;
struct PlayerManager;
struct Camera;
struct Target;

struct PlayerAI
{
	struct Memory
	{
		Vec3 pos;
		Vec3 velocity;
		Ref<Entity> entity;
	};

	static PinArray<PlayerAI, MAX_PLAYERS> list;

	static AI::Config generate_config(AI::Team, r32);

	Array<Memory> memory;
	Ref<PlayerManager> manager;
	Revision revision;
	AI::Config config;
	b8 spawning;

	PlayerAI(PlayerManager*, const AI::Config&);
	inline ID id() const
	{
		return ID(this - &list[0]);
	}
	void update_server(const Update&);
	void spawn(const SpawnPosition&);
	void spawn_callback(const AI::DronePathNode&);
};

struct ActionEntry
{
	s32 priority;
	AI::RecordedLife::Action action;
};

struct ActionEntryKey
{
	r32 priority(const ActionEntry& e)
	{
		return r32(e.priority);
	}
};

struct FailedAction
{
	r32 timestamp;
	AI::RecordedLife::Action action;
};

struct PlayerControlAI : public ComponentType<PlayerControlAI>
{
#if DEBUG_AI_CONTROL
	Ref<Camera> camera;
#endif

	PriorityQueue<ActionEntry, ActionEntryKey> action_queue;
	StaticArray<FailedAction, 4> recent_failed_actions;
	ActionEntry current;
	Vec3 target_pos;
	Vec3 random_look;
	u32 active_callback;
	r32 aim_timeout; // time we've been able to shoot but haven't due to aiming
	r32 aim_timer; // total aim time including cooldowns etc.
	r32 reeval_timer;
	r32 inaccuracy;
	AI::DronePath path;
	s32 path_index;
	Ref<PlayerAI> player;
	Ref<Entity> target;
	b8 target_shot_at;
	b8 target_hit;
	b8 target_active;
	ActionEntryKey action_queue_key;

	PlayerControlAI(PlayerAI* = nullptr);
	void awake();
	~PlayerControlAI();

	void action_clear();
	void action_execute(const ActionEntry&);
	void action_done(b8);
	void actions_populate();

	void callback_path(const AI::DroneResult&);
	void upgrade_completed(Upgrade);
	void update_memory();
	Vec2 aim(const Update&, const Vec3&, r32);
	void aim_and_shoot_target(const Update&, const Vec3&, Target*);
	b8 aim_and_shoot_location(const Update&, const AI::DronePathNode&, const AI::DronePathNode&, r32);
	b8 in_range(const Vec3&, r32) const;
	void set_path(const AI::DronePath&);
	void drone_done_flying_or_dashing();
	void drone_hit(Entity*);
	void drone_detaching();
	void update_server(const Update&);
	const AI::Config& config() const;
};


}