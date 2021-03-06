#pragma once
#include "types.h"
#include "game.h"

namespace VI
{

typedef void (*ScriptFunction)(const EntityFinder&);

struct Script
{
	static Script list[];
	static s32 count;

	static AssetID find(const char*);

	const char* name;
	ScriptFunction function;
};

namespace Scripts
{

namespace Docks
{
	void play();
	void gamejolt_prompt();
}

namespace AudioLogs
{
	AssetID get_id(const char*);
	void play(AssetID);
	void stop();
}

}

}
